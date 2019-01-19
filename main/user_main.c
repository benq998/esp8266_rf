/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include "rom/ets_sys.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "user_main.h"
#include "user_wifi.h"
#include "user_busi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "lwip/apps/sntp.h"
#include "driver/uart.h"
#include "esp_log.h"

static EventGroupHandle_t main_event_group;
static const int MAIN_CONNECTED_BIT = BIT0;

void init_sntp();

//wifi状态回调通知
void wifi_status_callback_impl(wifi_status status){
	if(status == wifi_connected){
		//wifi连接成功
		xEventGroupSetBits(main_event_group, MAIN_CONNECTED_BIT);
	}else if(status == wifi_disconn){
		//wifi断开
		xEventGroupClearBits(main_event_group, MAIN_CONNECTED_BIT);
	}else{
		//其他不支持的状态
		ESP_LOGI(TAG, "其他未知的状态回调:%d", status);
	}
}

void init_sntp(){
	struct timeval now;
	int sntp_retry_cnt = 0;
	int sntp_retry_time = 500;
	sntp_setoperatingmode(SNTP_OPMODE_POLL);
	int i = 0;
//	sntp_setservername(i++, "cn.ntp.org.cn");
//	sntp_setservername(i++,"0.cn.pool.ntp.org");
	sntp_setservername(i++,"1.cn.pool.ntp.org");
	sntp_setservername(i++,"2.cn.pool.ntp.org");
	sntp_init();
	while (1) {
		for (int32_t i = 0; (i < (SNTP_RECV_TIMEOUT / 100)) && now.tv_sec < 1546272000; i++) {
			vTaskDelay(100 / portTICK_RATE_MS);
			//延时
			gettimeofday(&now, NULL);
		}

		if (now.tv_sec < 1546272000) {//20190101
			sntp_retry_time = SNTP_RECV_TIMEOUT << sntp_retry_cnt;

			if (SNTP_RECV_TIMEOUT << (sntp_retry_cnt + 1) < SNTP_RETRY_TIMEOUT_MAX) {
				sntp_retry_cnt ++;
			}

			ESP_LOGI(TAG, "SNTP get time failed, retry after %d ms", sntp_retry_time);
			vTaskDelay(sntp_retry_time / portTICK_RATE_MS);
		} else {
			ESP_LOGI(TAG, "SNTP get time success");
			break;
		}
	}
	ESP_LOGI(TAG, "currentTimeSeconds:%d", currentTimeSeconds());
	sntp_stop();
}

int currentTimeSeconds(){
	struct timeval now;
	gettimeofday(&now, NULL);
	return (int)now.tv_sec;
}

void main_task(void *pv){
	ESP_LOGI(TAG, "main_task start!!!");
	//等待wifi连接完成
	xEventGroupWaitBits(main_event_group, MAIN_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
	init_sntp();
	start_busi();
}

void init_uart(){
	//初始化串口
	uart_config_t uart_config = {
		.baud_rate = 115200,
		.data_bits = UART_DATA_8_BITS,
		.parity    = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE
	};
	uart_param_config(UART_NUM_0, &uart_config);
	uart_driver_install(UART_NUM_0, UART_BUF_SIZE * 2, 0, 0, NULL);
}

void app_main(void)
{
	init_uart();
	ESP_LOGI(TAG, "SDK version:%s", esp_get_idf_version());
    //Initialize NVS
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
	  ESP_ERROR_CHECK(nvs_flash_erase());
	  ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	main_event_group = xEventGroupCreate();

	register_wifi_callback(wifi_status_callback_impl);
	call_wifi_init();
	xTaskCreate(main_task, "main_task", 4096, NULL, 10, NULL);
}
