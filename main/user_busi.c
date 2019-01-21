#include <stdlib.h>
#include "user_busi.h"
#include "user_tcp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "user_main.h"


//连接状态变化会带哦
void conn_status_changed(tcp_status st){
	//ESP_LOGI(TAG, "conn_status_changed:%d", st);
}

//收到的网络消息处理
void process_message(char *buf, int length){
	//这里收到的消息是业务数据，不在包括前面的36 50 len 3个字节
	//业务消息格式：type msg
	//type:0-心跳，在tcp层已经处理过,1-射频数据
	//msg：type=1的时候是射频数据，原封不动写到串口即可
	int type = buf[0];
	if(type == 1){
		//射频控制数据
		//ESP_LOGI(TAG,"dataLen:%d", length);
		for(int i=1;i<length;i++){
			//ESP_LOGI(TAG,"=>%02X", buf[i]);
		}
		uart_write_bytes(UART_NUM_0, buf + 1, length - 1);
	}else{
		//还不支持的数据
	}
}

void loop_read_uart_forever(){
	uint8_t *data_buffer_ptr = (uint8_t *) malloc(UART_BUF_SIZE);
	for(;;){
		//ESP_LOGI(TAG, "loop_read_uart_forever");
		int len = uart_read_bytes(UART_NUM_0, data_buffer_ptr, UART_BUF_SIZE, portMAX_DELAY);
		if(len > 0){
			// Write data back to tcp
			//ESP_LOGI(TAG, "send uart data to tcp %d bytes.", len);
			send_data((char*)data_buffer_ptr, len);
		}else{
			vTaskDelay(50 / portTICK_RATE_MS);
		}
	}
}


void start_busi(){

	register_callback(conn_status_changed, process_message);

	init_tcp_conn();

//	loop_read_uart_forever();//这个方法不返回
}

