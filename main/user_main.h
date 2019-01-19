#ifndef __ESP_USER_MAIN_H__
#define __ESP_USER_MAIN_H__

#include "esp_types.h"

#define TAG "rf_315_433"

#define EXAMPLE_ESP_WIFI_SSID      "GTX"
#define EXAMPLE_ESP_WIFI_PASS      "20160906"
#define EXAMPLE_MAX_STA_CONN       4

#define server_ip		"140.143.161.37"
#define server_port		7890

#define UART_BUF_SIZE (1024)		//读缓冲区长度

//wifi回调状态定义
typedef enum {
	wifi_connected = 0,		//wifi连接，切得到ip
	wifi_disconn			//wifi端口或丢失ip
} wifi_status;

//wifi回调方法定义
typedef void (*wifi_status_callback)(wifi_status status);

int currentTimeSeconds();

#endif
