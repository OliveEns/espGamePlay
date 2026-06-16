/**
 * @file game_manager.c
 * @brief 游戏管理器实现
 * @author Oliver
 * @date 2026
 */

#include "game_manager.h"
#include "game_task.h"
#include "lua_binding.h"
#include "esp_log.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "esp_partition.h"
#include "esp_heap_caps.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

static const char *TAG = "GAME_MANAGER";

// 挂载点
#define MOUNT_POINT "/game"

// 全局变量
static lua_State *game_lua_state = NULL;
static uint8_t *game_script_data = NULL;
static size_t game_script_size = 0;
static char current_game_name[GAME_NAME_MAX_LEN + 1] = {0};

// CRC32表
static uint32_t crc32_table[256];

/**
 * @brief 初始化CRC32表
 */
static void crc32_init(void) {
    uint32_t c;
    int i, j;
    
    for (i = 0; i < 256; i++) {
        c = (uint32_t)i;
        for (j = 0; j < 8; j++) {
            if (c & 1) {
                c = 0xedb88320L ^ (c >> 1);
            } else {
                c = c >> 1;
            }
        }
        crc32_table[i] = c;
    }
}

/**
 * @brief 计算CRC32
 */
uint32_t crc32(const uint8_t *data, size_t size) {
    static bool initialized = false;
    if (!initialized) {
        crc32_init();
        initialized = true;
    }
    
    uint32_t crc = 0xffffffffL;
    while (size--) {
        crc = crc32_table[(crc ^ *data++) & 0xff] ^ (crc >> 8);
    }
    return crc ^ 0xffffffffL;
}

/**
 * @brief 初始化文件系统
 */
esp_err_t game_fs_init(void) {
    ESP_LOGI(TAG, "初始化游戏文件系统...");
    
    // 检查是否已挂载
    struct stat st;
    if (stat(MOUNT_POINT, &st) == 0) {
        ESP_LOGI(TAG, "文件系统已挂载");
        return ESP_OK;
    }
    
    // 查找game_storage分区
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
        "game_storage"
    );
    
    if (partition == NULL) {
        ESP_LOGE(TAG, "未找到game_storage分区");
        return ESP_ERR_NOT_FOUND;
    }
    
    // 配置SPIFFS
    esp_vfs_spiffs_conf_t conf = {
        .base_path = MOUNT_POINT,
        .partition_label = "game_storage",
        .max_files = 5,
        .format_if_mount_failed = true,
    };
    
    // 挂载SPIFFS
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "挂载SPIFFS失败: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 获取分区信息
    size_t total = 0, used = 0;
    ret = esp_spiffs_info("game_storage", &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "获取SPIFFS信息失败: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "SPIFFS挂载成功 - 总大小: %d KB, 已用: %d KB", total / 1024, used / 1024);
    game_list_files_debug();   // 打印文件列表
    
    return ESP_OK;
}

/**
 * @brief 验证游戏文件头
 */
static bool validate_game_header(const game_header_t *header) {
    // 检查魔数
    if (memcmp(header->magic, GAME_MAGIC, GAME_MAGIC_SIZE) != 0) {
        return false;
    }
    
    // 检查版本
    if (header->version != GAME_FORMAT_VERSION) {
        return false;
    }
    
    // 检查名称长度
    if (header->game_name_len > GAME_NAME_MAX_LEN) {
        return false;
    }
    
    return true;
}

/**
 * @brief 扫描游戏列表
 */
esp_err_t game_list_scan(game_info_t *games, int max_count, int *out_count) {
    if (games == NULL || out_count == NULL || max_count <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *out_count = 0;
    DIR *dir = opendir(MOUNT_POINT);
    if (dir == NULL) {
        ESP_LOGE(TAG, "打开目录失败");
        return ESP_ERR_NOT_FOUND;
    }
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && *out_count < max_count) {
        // 只处理.game文件
        size_t len = strlen(entry->d_name);
        if (len <= 5 || strcmp(entry->d_name + len - 5, GAME_FILE_EXT) != 0) {
            continue;
        }
        
        // 打开文件读取头
        char filepath[512];
        snprintf(filepath, sizeof(filepath), "%s/%s", MOUNT_POINT, entry->d_name);
        
        FILE *f = fopen(filepath, "rb");
        if (f == NULL) {
            continue;
        }
        
        game_header_t header;
        size_t read = fread(&header, 1, GAME_HEADER_SIZE, f);
        
        // 获取文件大小
        fseek(f, 0, SEEK_END);
        size_t file_size = ftell(f);
        fclose(f);
        
        if (read != GAME_HEADER_SIZE || !validate_game_header(&header)) {
            continue;
        }
        
        // 填充游戏信息
        game_info_t *info = &games[*out_count];
        memset(info, 0, sizeof(game_info_t));
        
        // 复制游戏名称
        int name_len = header.game_name_len < GAME_NAME_MAX_LEN ? header.game_name_len : GAME_NAME_MAX_LEN;
        memcpy(info->name, header.game_name, name_len);
        info->name[name_len] = '\0';
        
        // 复制作者
        strncpy(info->author, header.author, AUTHOR_NAME_MAX_LEN);
        
        // 脚本大小
        info->script_size = header.script_size;
        info->file_size = file_size;
        strncpy(info->filename, entry->d_name, sizeof(info->filename) - 1);
        
        (*out_count)++;
    }
    
    closedir(dir);
    
    ESP_LOGI(TAG, "扫描到 %d 个游戏", *out_count);
    return ESP_OK;
}

/**
 * @brief 加载游戏
 */
esp_err_t game_load(const char *name, uint8_t **out_script, size_t *out_size) {
    if (name == NULL || out_script == NULL || out_size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *out_script = NULL;
    *out_size = 0;
    
    char filepath[512];
    
    // 检查是否带扩展名
    size_t len = strlen(name);
    if (len <= 5 || strcmp(name + len - 5, GAME_FILE_EXT) != 0) {
        snprintf(filepath, sizeof(filepath), "%s/%s%s", MOUNT_POINT, name, GAME_FILE_EXT);
    } else {
        snprintf(filepath, sizeof(filepath), "%s/%s", MOUNT_POINT, name);
    }
    
    FILE *f = fopen(filepath, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "打开游戏文件失败: %s", filepath);
        return ESP_ERR_NOT_FOUND;
    }
    
    // 读取文件头
    game_header_t header;
    size_t read = fread(&header, 1, GAME_HEADER_SIZE, f);
    if (read != GAME_HEADER_SIZE || !validate_game_header(&header)) {
        fclose(f);
        ESP_LOGE(TAG, "游戏文件头无效");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 读取脚本数据
    *out_size = header.script_size;
    *out_script = (uint8_t *)malloc(*out_size);
    if (*out_script == NULL) {
        fclose(f);
        ESP_LOGE(TAG, "内存分配失败");
        return ESP_ERR_NO_MEM;
    }
    
    read = fread(*out_script, 1, *out_size, f);
    fclose(f);
    
    if (read != *out_size) {
        free(*out_script);
        *out_script = NULL;
        *out_size = 0;
        ESP_LOGE(TAG, "读取脚本数据失败");
        return ESP_ERR_INVALID_SIZE;
    }
    
    // 验证CRC
    uint32_t calc_crc = crc32(*out_script, *out_size);
    if (calc_crc != header.script_crc) {
        free(*out_script);
        *out_script = NULL;
        *out_size = 0;
        ESP_LOGE(TAG, "CRC校验失败: 期望 0x%08lx, 实际 0x%08lx", header.script_crc, calc_crc);
        return ESP_ERR_INVALID_CRC;
    }
    
    ESP_LOGI(TAG, "游戏加载成功: %s, 脚本大小: %d bytes", name, *out_size);
    return ESP_OK;
}

/**
 * @brief 删除游戏
 */
esp_err_t game_delete(const char *name) {
    if (name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char filepath[512];
    
    // 检查是否带扩展名
    size_t len = strlen(name);
    if (len <= 5 || strcmp(name + len - 5, GAME_FILE_EXT) != 0) {
        snprintf(filepath, sizeof(filepath), "%s/%s%s", MOUNT_POINT, name, GAME_FILE_EXT);
    } else {
        snprintf(filepath, sizeof(filepath), "%s/%s", MOUNT_POINT, name);
    }
    
    int ret = remove(filepath);
    if (ret != 0) {
        ESP_LOGE(TAG, "删除游戏失败: %s", filepath);
        return ESP_ERR_NOT_FOUND;
    }
    
    ESP_LOGI(TAG, "游戏删除成功: %s", name);
    return ESP_OK;
}

/**
 * @brief 创建游戏文件
 */
esp_err_t game_create(const game_header_t *header, const uint8_t *script, size_t script_size) {
    if (header == NULL || script == NULL || script_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 验证头
    if (memcmp(header->magic, GAME_MAGIC, GAME_MAGIC_SIZE) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 生成文件名（基于游戏名称）
    char filename[64];
    snprintf(filename, sizeof(filename), "%s%s", header->game_name, GAME_FILE_EXT);
    
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", MOUNT_POINT, filename);
    
    // 创建文件
    FILE *f = fopen(filepath, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "创建游戏文件失败: %s", filepath);
        return ESP_ERR_NO_MEM;
    }
    
    // 写入文件头
    game_header_t write_header = *header;
    write_header.script_size = script_size;
    write_header.script_crc = crc32(script, script_size);
    
    size_t written = fwrite(&write_header, 1, GAME_HEADER_SIZE, f);
    if (written != GAME_HEADER_SIZE) {
        fclose(f);
        remove(filepath);
        ESP_LOGE(TAG, "写入文件头失败");
        return ESP_ERR_INVALID_SIZE;
    }
    
    // 写入脚本数据
    written = fwrite(script, 1, script_size, f);
    fclose(f);
    
    if (written != script_size) {
        remove(filepath);
        ESP_LOGE(TAG, "写入脚本数据失败");
        return ESP_ERR_INVALID_SIZE;
    }
    
    ESP_LOGI(TAG, "游戏创建成功: %s, 脚本大小: %d bytes", header->game_name, script_size);
    return ESP_OK;
}

/**
 * @brief 启动游戏
 */
esp_err_t game_run(const char* game_name) {
    if (game_name == NULL || strlen(game_name) == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 检查是否已有游戏在运行
    if (game_lua_state != NULL) {
        ESP_LOGE(TAG, "已有游戏在运行: %s", current_game_name);
        return ESP_ERR_INVALID_STATE;
    }
    
    // 打印剩余堆大小
    size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    ESP_LOGI(TAG, "启动游戏前剩余堆: %d bytes", free_heap);
    
    ESP_LOGI(TAG, "启动游戏: %s", game_name);
    
    // 加载游戏脚本
    uint8_t *script = NULL;
    size_t script_size = 0;
    esp_err_t ret = game_load(game_name, &script, &script_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "加载游戏失败: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 保存脚本数据用于后续释放
    game_script_data = script;
    game_script_size = script_size;
    strncpy(current_game_name, game_name, GAME_NAME_MAX_LEN);
    
    // 创建Lua状态机
    lua_State *L = luaL_newstate();
    if (L == NULL) {
        ESP_LOGE(TAG, "创建Lua状态机失败");
        free(script);
        game_script_data = NULL;
        return ESP_ERR_NO_MEM;
    }
    game_lua_state = L;
    
    // 打开标准库
    luaL_openlibs(L);
    
    // 注册Game API
    lua_binding_register_game(L);
    
    // 加载脚本
    ret = luaL_loadbuffer(L, (const char *)script, script_size, game_name);
    if (ret != LUA_OK) {
        ESP_LOGE(TAG, "加载Lua脚本失败: %s", lua_tostring(L, -1));
        lua_close(L);
        game_lua_state = NULL;
        free(script);
        game_script_data = NULL;
        return ESP_ERR_INVALID_ARG;
    }
    
    // 执行全局代码
    ret = lua_pcall(L, 0, 0, 0);
    if (ret != LUA_OK) {
        ESP_LOGE(TAG, "执行Lua脚本失败: %s", lua_tostring(L, -1));
        lua_close(L);
        game_lua_state = NULL;
        free(script);
        game_script_data = NULL;
        return ESP_ERR_INVALID_ARG;
    }
    
    // 检查on_init函数
    lua_getglobal(L, "on_init");
    if (!lua_isfunction(L, -1)) {
        ESP_LOGE(TAG, "脚本缺少on_init函数");
        lua_close(L);
        game_lua_state = NULL;
        free(script);
        game_script_data = NULL;
        return ESP_ERR_INVALID_ARG;
    }
    lua_pop(L, 1);
    
    // 检查on_update函数
    lua_getglobal(L, "on_update");
    if (!lua_isfunction(L, -1)) {
        ESP_LOGE(TAG, "脚本缺少on_update函数");
        lua_close(L);
        game_lua_state = NULL;
        free(script);
        game_script_data = NULL;
        return ESP_ERR_INVALID_ARG;
    }
    lua_pop(L, 1);
    
    // 检查on_render函数
    lua_getglobal(L, "on_render");
    if (!lua_isfunction(L, -1)) {
        ESP_LOGE(TAG, "脚本缺少on_render函数");
        lua_close(L);
        game_lua_state = NULL;
        free(script);
        game_script_data = NULL;
        return ESP_ERR_INVALID_ARG;
    }
    lua_pop(L, 1);
    
    // 调用on_init
    lua_getglobal(L, "on_init");
    ret = lua_pcall(L, 0, 0, 0);
    if (ret != LUA_OK) {
        ESP_LOGE(TAG, "调用on_init失败: %s", lua_tostring(L, -1));
        lua_close(L);
        game_lua_state = NULL;
        free(script);
        game_script_data = NULL;
        return ESP_ERR_INVALID_ARG;
    }
    
    // 创建游戏任务
    game_task_create(L, game_name);
    
    // 打印剩余堆大小
    free_heap = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    ESP_LOGI(TAG, "启动游戏后剩余堆: %d bytes", free_heap);
    
    ESP_LOGI(TAG, "游戏启动成功: %s", game_name);
    return ESP_OK;
}

/**
 * @brief 强制停止当前游戏
 */
void game_stop(void) {
    if (game_lua_state == NULL) {
        ESP_LOGW(TAG, "没有正在运行的游戏");
        return;
    }
    
    ESP_LOGI(TAG, "停止游戏: %s", current_game_name);
    
    // 请求游戏任务退出
    game_task_request_exit();
    
    // 等待任务结束
    game_task_wait_exit();
    
    // 关闭Lua状态机
    if (game_lua_state != NULL) {
        lua_close(game_lua_state);
        game_lua_state = NULL;
    }
    
    // 释放脚本内存
    if (game_script_data != NULL) {
        free(game_script_data);
        game_script_data = NULL;
        game_script_size = 0;
    }
    
    // 清空游戏名称
    memset(current_game_name, 0, sizeof(current_game_name));
    
    // 打印剩余堆大小
    size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    ESP_LOGI(TAG, "游戏停止后剩余堆: %d bytes", free_heap);
    
    ESP_LOGI(TAG, "游戏停止成功");
}

/**
 * @brief 获取游戏存储使用信息
 */
esp_err_t game_get_storage_info(size_t *out_total, size_t *out_used) {
    if (out_total == NULL || out_used == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_spiffs_info("game_storage", out_total, out_used);
}

/**
 * @brief 递归打印目录树（内部函数）
 * @param path 当前路径
 * @param depth 深度（用于缩进）
 */
static void print_dir_tree_recursive(const char *path, int depth) {
    DIR *dir = opendir(path);
    if (!dir) {
        ESP_LOGE(TAG, "无法打开目录: %s", path);
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // 跳过 . 和 ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        char fullpath[512];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);
        struct stat st;
        if (stat(fullpath, &st) != 0) {
            ESP_LOGW(TAG, "stat 失败: %s", fullpath);
            continue;
        }
        // 打印缩进
        char indent[32] = {0};
        for (int i = 0; i < depth; i++) {
            strcat(indent, "  ");
        }
        if (S_ISDIR(st.st_mode)) {
            ESP_LOGI(TAG, "%s[DIR]  %s/", indent, entry->d_name);
            print_dir_tree_recursive(fullpath, depth + 1);
        } else {
            ESP_LOGI(TAG, "%s[FILE] %s (%ld bytes)", indent, entry->d_name, (long)st.st_size);
        }
    }
    closedir(dir);
}

/**
 * @brief 调试函数：打印 /game 目录下所有文件（递归树形）
 */
void game_list_files_debug(void) {
    ESP_LOGI(TAG, "=== 目录树 (%s) ===", MOUNT_POINT);
    print_dir_tree_recursive(MOUNT_POINT, 0);
    ESP_LOGI(TAG, "==================");
}

/**
 * @brief 游戏任务退出时的回调（由任务调用，清理管理器状态）
 */
void game_on_task_exit(void) {
    if (game_lua_state != NULL) {
        ESP_LOGI(TAG, "游戏任务退出，清理Lua状态机");
        lua_close(game_lua_state);
        game_lua_state = NULL;
    }
    if (game_script_data != NULL) {
        free(game_script_data);
        game_script_data = NULL;
        game_script_size = 0;
    }
    memset(current_game_name, 0, sizeof(current_game_name));
}