#ifndef __ESP_USER_WIFI_H__
#define __ESP_USER_WIFI_H__

#include "user_main.h"

// 关于wifi的操作都在这里定义

void register_wifi_callback(wifi_status_callback callback);

void call_wifi_init();


#endif
