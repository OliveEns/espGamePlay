/**
 * @file download_manager.c
 * @brief 统一下载管理：串口(UART API) + WiFi(SoftAP TCP)
 * @author Oliver
 * @date 2026
 */

#include "download_manager.h"
#include "game_manager.h"
#include "esp_log.h"
#include "esp_crc.h"
#include "driver/uart.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

extern void game_list_files_debug(void);

static const char *TAG = "DOWNLOAD_MANAGER";

// 协议定义
#define FRAME_HEADER1 0x55
#define FRAME_HEADER2 0xAA

typedef enum {
    CMD_HANDSHAKE = 0x01,
    CMD_UPLOAD_START = 0x02,
    CMD_UPLOAD_DATA = 0x03,
    CMD_UPLOAD_END = 0x04,
    CMD_UPLOAD_CANCEL = 0x05,
    CMD_HANDSHAKE_RESP = 0x81,
    CMD_UPLOAD_ACK = 0x82,
    CMD_UPLOAD_PROGRESS = 0x83,
} cmd_t;

typedef enum {
    STATUS_OK = 0x00,
    STATUS_FILE_OPEN_ERR = 0x01,
    STATUS_WRITE_ERR = 0x02,
    STATUS_CRC_ERR = 0x03,
    STATUS_BUSY = 0x04,
} status_t;

// 下载上下文
typedef struct {
    char filename[64];
    uint32_t file_size;
    uint32_t received_bytes;
    uint32_t expected_offset;
    FILE *file;
    bool active;
} download_ctx_t;

static download_ctx_t s_ctx = {0};
static TaskHandle_t s_download_task = NULL;

// ---------- CRC16/CCITT ----------
static uint16_t crc16_ccitt(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (data[i] << 8);
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return crc;
}

/**
 * @brief 从字节流中解析一帧，并返回帧总长度（用于滑动窗口）
 * @param buf 接收缓冲区
 * @param len 缓冲区已有字节数
 * @param out_cmd 输出命令
 * @param out_data 输出数据（需至少 2048 字节）
 * @param out_data_len 输出数据长度
 * @param out_frame_len 输出帧总长度（从帧头到CRC结束）
 * @return ESP_OK 解析成功，ESP_ERR_INVALID_SIZE 需要更多数据，其他错误
 */
static esp_err_t parse_frame_ex(const uint8_t *buf, size_t len,
                                cmd_t *out_cmd, uint8_t *out_data, size_t *out_data_len,
                                size_t *out_frame_len) {
    if (len < 7) return ESP_ERR_INVALID_SIZE;
    
    // 查找帧头
    size_t header_pos = 0;
    while (header_pos <= len - 2) {
        if (buf[header_pos] == FRAME_HEADER1 && buf[header_pos+1] == FRAME_HEADER2) break;
        header_pos++;
    }
    if (header_pos > len - 2) return ESP_ERR_INVALID_SIZE;
    
    const uint8_t *frame = buf + header_pos;
    size_t remaining = len - header_pos;
    if (remaining < 7) return ESP_ERR_INVALID_SIZE;
    
    cmd_t cmd = (cmd_t)frame[2];
    uint16_t data_len = (frame[3] << 8) | frame[4];
    size_t total_len = 7 + data_len;   // 帧头2+命令1+长度2+数据+CRC2
    if (remaining < total_len) return ESP_ERR_INVALID_SIZE;
    
    uint16_t recv_crc = (frame[5+data_len] << 8) | frame[6+data_len];
    uint16_t calc_crc = crc16_ccitt(frame+2, 3 + data_len);
    if (calc_crc != recv_crc) {
        ESP_LOGW(TAG, "CRC mismatch, dropping frame");
        return ESP_ERR_INVALID_CRC;
    }
    
    *out_cmd = cmd;
    *out_data_len = data_len;
    if (data_len > 0) {
        if (data_len > 2048) {
            ESP_LOGE(TAG, "Data too large: %d", data_len);
            return ESP_ERR_INVALID_SIZE;
        }
        memcpy(out_data, frame+5, data_len);
    }
    *out_frame_len = header_pos + total_len;
    return ESP_OK;
}

// ---------- 通用命令处理 ----------
typedef void (*send_response_func_t)(void *ctx, cmd_t cmd, const uint8_t *data, size_t len);

static void handle_command_generic(cmd_t cmd, uint8_t *data, size_t len,
                                   void *send_ctx, send_response_func_t send_func) {
    ESP_LOGI(TAG, "handle_command: cmd=0x%02X, len=%d, active=%d", cmd, len, s_ctx.active);
    
    switch (cmd) {
        case CMD_HANDSHAKE: {
            char info[] = "ESP32-C3 Game v1.0";
            send_func(send_ctx, CMD_HANDSHAKE_RESP, (uint8_t*)info, strlen(info)+1);
            break;
        }
        case CMD_UPLOAD_START: {
            // 只有当已经有打开的文件（即正在下载中）时才拒绝
            if (s_ctx.file != NULL) {
                uint8_t err = STATUS_BUSY;
                ESP_LOGW(TAG, "UPLOAD_START rejected: file already open");
                send_func(send_ctx, CMD_UPLOAD_ACK, &err, 1);
                break;
            }
            const char *fname = (const char*)data;
            size_t fname_len = strlen(fname);
            if (fname_len + 1 + 8 > len) {
                uint8_t err = STATUS_FILE_OPEN_ERR;
                send_func(send_ctx, CMD_UPLOAD_ACK, &err, 1);
                break;
            }
            uint32_t total_size = (data[fname_len+1] << 24) | (data[fname_len+2] << 16) |
                                  (data[fname_len+3] << 8) | data[fname_len+4];
            uint32_t offset = (data[fname_len+5] << 24) | (data[fname_len+6] << 16) |
                              (data[fname_len+7] << 8) | data[fname_len+8];
            char path[64];
            snprintf(path, sizeof(path), "/game/%s.game", fname);
            FILE *f = fopen(path, offset > 0 ? "r+b" : "wb");
            if (!f) {
                uint8_t err = STATUS_FILE_OPEN_ERR;
                send_func(send_ctx, CMD_UPLOAD_ACK, &err, 1);
                break;
            }
            if (offset > 0) fseek(f, offset, SEEK_SET);
            s_ctx.file = f;
            strncpy(s_ctx.filename, fname, sizeof(s_ctx.filename)-1);
            s_ctx.file_size = total_size;
            s_ctx.received_bytes = offset;
            s_ctx.expected_offset = offset;
            s_ctx.active = true;
            ESP_LOGI(TAG, "UPLOAD_START accepted, active set true, file=%s, size=%lu", s_ctx.filename, total_size);
            uint8_t ack = STATUS_OK;
            send_func(send_ctx, CMD_UPLOAD_ACK, &ack, 1);
            break;
        }
        case CMD_UPLOAD_DATA: {
            if (!s_ctx.active || !s_ctx.file) {
                uint8_t err = STATUS_BUSY;
                ESP_LOGW(TAG, "UPLOAD_DATA rejected: active=%d, file=%p", s_ctx.active, s_ctx.file);
                send_func(send_ctx, CMD_UPLOAD_ACK, &err, 1);
                break;
            }
            if (len < 4) {
                uint8_t err = STATUS_WRITE_ERR;
                send_func(send_ctx, CMD_UPLOAD_ACK, &err, 1);
                break;
            }
            uint32_t offset = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
            const uint8_t *block = data + 4;
            size_t block_len = len - 4;
            if (offset != s_ctx.expected_offset) {
                uint8_t err = STATUS_WRITE_ERR;
                ESP_LOGW(TAG, "UPLOAD_DATA offset mismatch: expected=%lu, got=%lu", s_ctx.expected_offset, offset);
                send_func(send_ctx, CMD_UPLOAD_ACK, &err, 1);
                break;
            }
            size_t written = fwrite(block, 1, block_len, s_ctx.file);
            if (written != block_len) {
                ESP_LOGE(TAG, "fwrite failed: wrote %d of %d, errno=%d", written, block_len, errno);
                uint8_t err = STATUS_WRITE_ERR;
                send_func(send_ctx, CMD_UPLOAD_ACK, &err, 1);
                break;
            }
            s_ctx.received_bytes += block_len;
            s_ctx.expected_offset += block_len;
            uint8_t prog[4];
            prog[0] = (s_ctx.received_bytes >> 24) & 0xFF;
            prog[1] = (s_ctx.received_bytes >> 16) & 0xFF;
            prog[2] = (s_ctx.received_bytes >> 8) & 0xFF;
            prog[3] = s_ctx.received_bytes & 0xFF;
            send_func(send_ctx, CMD_UPLOAD_PROGRESS, prog, 4);
            break;
        }
        case CMD_UPLOAD_END: {
            if (!s_ctx.active || !s_ctx.file) {
                uint8_t err = STATUS_BUSY;
                send_func(send_ctx, CMD_UPLOAD_ACK, &err, 1);
                break;
            }
            if (len < 4) {
                uint8_t err = STATUS_CRC_ERR;
                send_func(send_ctx, CMD_UPLOAD_ACK, &err, 1);
                break;
            }
            uint32_t file_crc = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
            
            fclose(s_ctx.file);
            s_ctx.file = NULL;
            
            char path[128];
            snprintf(path, sizeof(path), "/game/%s.game", s_ctx.filename);
            ESP_LOGI(TAG, "UPLOAD_END: verifying %s", path);
            
            FILE *f = fopen(path, "rb");
            if (!f) {
                ESP_LOGE(TAG, "Cannot open saved file for CRC check: %s, errno=%d", path, errno);
                uint8_t err = STATUS_FILE_OPEN_ERR;
                send_func(send_ctx, CMD_UPLOAD_ACK, &err, 1);
                break;
            }
            fseek(f, 0, SEEK_END);
            long actual_size = ftell(f);
            fseek(f, 0, SEEK_SET);
            ESP_LOGI(TAG, "File size: %ld bytes", actual_size);
            
            uint8_t *buf = malloc(actual_size);
            if (!buf) {
                fclose(f);
                uint8_t err = STATUS_WRITE_ERR;
                send_func(send_ctx, CMD_UPLOAD_ACK, &err, 1);
                break;
            }
            fread(buf, 1, actual_size, f);
            fclose(f);
            uint32_t crc = crc32(buf, actual_size);
            free(buf);
            
            if (crc != file_crc) {
                ESP_LOGE(TAG, "CRC mismatch: expected 0x%08lx, got 0x%08lx. Deleting file.", file_crc, crc);
                remove(path);
                uint8_t err = STATUS_CRC_ERR;
                send_func(send_ctx, CMD_UPLOAD_ACK, &err, 1);
                break;
            }
            
            ESP_LOGI(TAG, "CRC match. Game saved successfully: %s", path);
            uint8_t ack = STATUS_OK;
            send_func(send_ctx, CMD_UPLOAD_ACK, &ack, 1);
            s_ctx.active = false;
            ESP_LOGI(TAG, "UPLOAD_END: active set false");
            break;
        }
        case CMD_UPLOAD_CANCEL: {
            if (s_ctx.file) {
                fclose(s_ctx.file);
                s_ctx.file = NULL;
                char path[128];
                snprintf(path, sizeof(path), "/game/%s.game", s_ctx.filename);
                remove(path);
            }
            s_ctx.active = false;
            ESP_LOGI(TAG, "UPLOAD_CANCEL: active set false");
            uint8_t ack = STATUS_OK;
            send_func(send_ctx, CMD_UPLOAD_ACK, &ack, 1);
            break;
        }
        default:
            break;
    }
}

// ---------- 串口实现 ----------
#define UART_RX_BUF_SIZE 4096
static int s_uart_num = -1;

static void send_response_uart(void *ctx, cmd_t cmd, const uint8_t *data, size_t len) {
    int uart_num = (int)(intptr_t)ctx;
    uint8_t frame[1024];
    int idx = 0;
    frame[idx++] = FRAME_HEADER1;
    frame[idx++] = FRAME_HEADER2;
    frame[idx++] = (uint8_t)cmd;
    frame[idx++] = (len >> 8) & 0xFF;
    frame[idx++] = len & 0xFF;
    if (data && len) {
        memcpy(frame + idx, data, len);
        idx += len;
    }
    uint16_t crc = crc16_ccitt(frame + 2, idx - 2);
    frame[idx++] = (crc >> 8) & 0xFF;
    frame[idx++] = crc & 0xFF;
    uart_write_bytes(uart_num, (const char*)frame, idx);
}

static void serial_download_task(void *arg) {
    int uart_num = (int)(intptr_t)arg;
    s_uart_num = uart_num;
    uint8_t rx_buf[UART_RX_BUF_SIZE];
    size_t buf_len = 0;
    TickType_t last_byte_time = xTaskGetTickCount();
    const TickType_t timeout_ticks = pdMS_TO_TICKS(2000);

    ESP_LOGI(TAG, "Serial download task started on UART%d", uart_num);

    while (s_ctx.active) {
        uint8_t c;
        int len = uart_read_bytes(uart_num, &c, 1, pdMS_TO_TICKS(200));
        if (len == 0) {
            if (xTaskGetTickCount() - last_byte_time > timeout_ticks) {
                ESP_LOGW(TAG, "Serial download timeout, exiting");
                break;
            }
            continue;
        }
        if (len < 0) {
            ESP_LOGE(TAG, "UART read error: %d", len);
            break;
        }
        if (buf_len >= sizeof(rx_buf)) {
            ESP_LOGW(TAG, "Buffer overflow, resetting");
            buf_len = 0;
            continue;
        }
        rx_buf[buf_len++] = c;
        last_byte_time = xTaskGetTickCount();

        cmd_t cmd;
        uint8_t data[2048];
        size_t data_len;
        size_t frame_len;
        esp_err_t err = parse_frame_ex(rx_buf, buf_len, &cmd, data, &data_len, &frame_len);
        if (err == ESP_OK) {
            handle_command_generic(cmd, data, data_len, (void*)(intptr_t)uart_num, send_response_uart);
            // 滑动窗口：移除已处理的帧
            if (frame_len < buf_len) {
                memmove(rx_buf, rx_buf + frame_len, buf_len - frame_len);
                buf_len -= frame_len;
            } else {
                buf_len = 0;
            }
        } else if (err == ESP_ERR_INVALID_CRC) {
            ESP_LOGW(TAG, "CRC error, resetting buffer");
            buf_len = 0;
        } else if (err != ESP_ERR_INVALID_SIZE) {
            ESP_LOGW(TAG, "Parse error %d, resetting buffer", err);
            buf_len = 0;
        }
        // err == ESP_ERR_INVALID_SIZE 时继续接收
    }

    if (s_ctx.file) {
        fclose(s_ctx.file);
        s_ctx.file = NULL;
    }
    s_ctx.active = false;
    ESP_LOGI(TAG, "Serial download task exited, active set false");
    s_download_task = NULL;
    vTaskDelete(NULL);
}

void download_start_serial(int uart_num, int baud_rate) {
    ESP_LOGI(TAG, "download_start_serial called, current task=%p, active=%d", s_download_task, s_ctx.active);
    if (s_download_task) {
        download_stop();
        while (s_download_task) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        ESP_LOGI(TAG, "Previous download task cleaned");
    }
    uart_config_t uart_config = {
        .baud_rate = baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(uart_num, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(uart_num, UART_RX_BUF_SIZE, UART_RX_BUF_SIZE, 0, NULL, 0));

    s_ctx.active = true;
    xTaskCreate(serial_download_task, "serial_dl", 8192, (void*)(intptr_t)uart_num, 5, &s_download_task);
    ESP_LOGI(TAG, "Serial download task created");
}

// ---------- WiFi 实现 ----------
static void wifi_init_softap(const char *ssid, const char *password) {
    static bool initialized = false;
    if (!initialized) {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_create_default_wifi_ap();
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        initialized = true;
    }

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "",
            .password = "",
            .ssid_len = 0,
            .max_connection = 1,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
        },
    };
    strncpy((char*)wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid)-1);
    if (password && strlen(password) > 0) {
        strncpy((char*)wifi_config.ap.password, password, sizeof(wifi_config.ap.password)-1);
    } else {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (netif) {
        esp_netif_dhcps_stop(netif);
        esp_netif_dhcps_start(netif);
    }
    ESP_LOGI(TAG, "SoftAP started. SSID:%s", ssid);
}

static void send_response_socket(void *ctx, cmd_t cmd, const uint8_t *data, size_t len) {
    int sock = (int)(intptr_t)ctx;
    uint8_t frame[1024];
    int idx = 0;
    frame[idx++] = FRAME_HEADER1;
    frame[idx++] = FRAME_HEADER2;
    frame[idx++] = (uint8_t)cmd;
    frame[idx++] = (len >> 8) & 0xFF;
    frame[idx++] = len & 0xFF;
    if (data && len) {
        memcpy(frame + idx, data, len);
        idx += len;
    }
    uint16_t crc = crc16_ccitt(frame + 2, idx - 2);
    frame[idx++] = (crc >> 8) & 0xFF;
    frame[idx++] = crc & 0xFF;
    send(sock, frame, idx, 0);
}

static void wifi_download_task(void *arg) {
    uint16_t port = (uint16_t)(intptr_t)arg;
    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Socket create failed");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "Bind failed");
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }
    listen(listen_sock, 1);
    ESP_LOGI(TAG, "WiFi download server on port %d, IP: 192.168.4.1", port);

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_sock = accept(listen_sock, (struct sockaddr*)&client_addr, &client_len);
    close(listen_sock);
    if (client_sock < 0) {
        ESP_LOGE(TAG, "Accept failed");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Client connected");

    uint8_t rx_buf[UART_RX_BUF_SIZE];
    size_t buf_len = 0;
    TickType_t last_byte_time = xTaskGetTickCount();
    const TickType_t timeout_ticks = pdMS_TO_TICKS(5000);
    struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 };
    setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (s_ctx.active) {
        uint8_t c;
        int len = recv(client_sock, &c, 1, 0);
        if (len == 0) {
            ESP_LOGI(TAG, "Client disconnected");
            break;
        }
        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (xTaskGetTickCount() - last_byte_time > timeout_ticks) {
                    ESP_LOGW(TAG, "WiFi download idle timeout, exiting");
                    break;
                }
                continue;
            } else {
                ESP_LOGE(TAG, "recv error: %d", errno);
                break;
            }
        }
        if (buf_len >= sizeof(rx_buf)) {
            ESP_LOGW(TAG, "Buffer overflow, resetting");
            buf_len = 0;
            continue;
        }
        rx_buf[buf_len++] = c;
        last_byte_time = xTaskGetTickCount();

        cmd_t cmd;
        uint8_t data[2048];
        size_t data_len;
        size_t frame_len;
        esp_err_t err = parse_frame_ex(rx_buf, buf_len, &cmd, data, &data_len, &frame_len);
        if (err == ESP_OK) {
            handle_command_generic(cmd, data, data_len, (void*)(intptr_t)client_sock, send_response_socket);
            // 滑动窗口：移除已处理的帧
            if (frame_len < buf_len) {
                memmove(rx_buf, rx_buf + frame_len, buf_len - frame_len);
                buf_len -= frame_len;
            } else {
                buf_len = 0;
            }
        } else if (err == ESP_ERR_INVALID_CRC) {
            ESP_LOGW(TAG, "CRC error, resetting buffer");
            buf_len = 0;
        } else if (err != ESP_ERR_INVALID_SIZE) {
            ESP_LOGW(TAG, "Parse error %d, resetting buffer", err);
            buf_len = 0;
        }
    }

    close(client_sock);
    download_stop();  // 设置 s_ctx.active = false
    ESP_LOGI(TAG, "WiFi download task exiting, active set false");
    game_list_files_debug();
    vTaskDelete(NULL);
}

void download_start_wifi(const char *ssid, const char *password, uint16_t port) {
    ESP_LOGI(TAG, "download_start_wifi called, current task=%p, active=%d", s_download_task, s_ctx.active);
    if (s_download_task) {
        ESP_LOGW(TAG, "Download already active, rejecting");
        return;
    }
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init_softap(ssid ? ssid : "ESP_Game_AP", password ? password : "12345678");
    s_ctx.active = true;
    xTaskCreate(wifi_download_task, "wifi_dl", 12288, (void*)(intptr_t)port, 5, &s_download_task);
    ESP_LOGI(TAG, "WiFi download task created");
}

// ---------- 公共控制函数 ----------
void download_stop(void) {
    ESP_LOGI(TAG, "download_stop called, active=%d", s_ctx.active);
    if (s_download_task) {
        s_ctx.active = false;
        // 任务会在循环中退出
    }
}

bool download_is_active(void) {
    return s_ctx.active;
}