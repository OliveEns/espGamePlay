/**
 * @file game_manager.h
 * @brief 游戏管理器 - 游戏文件格式定义和管理API
 * @author Oliver
 * @date 2026
 */

#ifndef GAME_MANAGER_H
#define GAME_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// 游戏文件魔数
#define GAME_MAGIC "GM01"
#define GAME_MAGIC_SIZE 4

// 游戏格式版本
#define GAME_FORMAT_VERSION 0x01

// 游戏名称最大长度
#define GAME_NAME_MAX_LEN 32

// 作者名称最大长度
#define AUTHOR_NAME_MAX_LEN 16

// 游戏文件头大小（固定64字节）
#define GAME_HEADER_SIZE 64

// 最大游戏数量
#define MAX_GAMES 32

// 游戏文件扩展名
#define GAME_FILE_EXT ".game"

/**
 * @brief 游戏文件头结构（小端序）
 */
typedef struct {
    uint8_t magic[4];              // 魔数 'G','M','0','1'
    uint8_t version;               // 格式版本
    uint8_t reserved;              // 保留字段
    uint16_t game_name_len;        // 游戏名称长度
    char game_name[GAME_NAME_MAX_LEN];  // 游戏名称（UTF-8）
    char author[AUTHOR_NAME_MAX_LEN];   // 作者名称
    uint32_t script_crc;           // Lua脚本CRC32
    uint32_t script_size;          // Lua脚本大小（字节）
} __attribute__((packed)) game_header_t;

/**
 * @brief 游戏信息结构
 */
typedef struct {
    char name[GAME_NAME_MAX_LEN + 1];  // 游戏名称
    char author[AUTHOR_NAME_MAX_LEN + 1]; // 作者名称
    uint32_t script_size;          // 脚本大小
    uint32_t file_size;            // 文件总大小
    char filename[64];             // 文件名
} game_info_t;

/**
 * @brief 初始化文件系统
 * @return ESP_OK 成功，其他失败
 */
esp_err_t game_fs_init(void);

/**
 * @brief 扫描游戏列表
 * @param games 游戏信息数组指针
 * @param max_count 最大扫描数量
 * @param out_count 实际找到的游戏数量
 * @return ESP_OK 成功，其他失败
 */
esp_err_t game_list_scan(game_info_t *games, int max_count, int *out_count);

/**
 * @brief 加载游戏
 * @param name 游戏名称或文件名
 * @param out_script 输出脚本数据（需要调用者释放内存）
 * @param out_size 输出脚本大小
 * @return ESP_OK 成功，其他失败
 */
esp_err_t game_load(const char *name, uint8_t **out_script, size_t *out_size);

/**
 * @brief 删除游戏
 * @param name 游戏名称或文件名
 * @return ESP_OK 成功，其他失败
 */
esp_err_t game_delete(const char *name);

/**
 * @brief 创建游戏文件
 * @param header 游戏文件头
 * @param script Lua脚本数据
 * @param script_size 脚本大小
 * @return ESP_OK 成功，其他失败
 */
esp_err_t game_create(const game_header_t *header, const uint8_t *script, size_t script_size);

/**
 * @brief 计算CRC32
 * @param data 数据指针
 * @param size 数据大小
 * @return CRC32值
 */
uint32_t crc32(const uint8_t *data, size_t size);

/**
 * @brief 启动游戏
 * @param game_name 游戏名称
 * @return ESP_OK 成功，其他失败
 */
esp_err_t game_run(const char* game_name);

/**
 * @brief 强制停止当前游戏
 */
void game_stop(void);

/**
 * @brief 调试函数：打印 /game 目录下所有文件
 */
void game_list_files_debug(void);

/**
 * @brief 游戏任务退出时的回调（由任务调用，清理管理器状态）
 */
void game_on_task_exit(void);

#ifdef __cplusplus
}
#endif

#endif // GAME_MANAGER_H