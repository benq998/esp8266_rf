#include <stdlib.h>
#include "user_busi.h"
#include "user_tcp.h"
#include "freertos/FreeRTOS.h"
#include "driver/uart.h"
#include "esp_log.h"

#define UART_BUF_SIZE (1024)		//读缓冲区长度
static uint8_t *data_buffer_ptr;	//读缓冲区

//连接状态变化会带哦
void conn_status_changed(tcp_status st){

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
		uart_write_bytes(UART_NUM_0,buf + 1,length - 1);
	}else{
		//还不支持的数据
	}
}

void loop_read_uart_forever(){
	for(;;){
		int len = uart_read_bytes(UART_NUM_0, data_buffer_ptr, UART_BUF_SIZE, portMAX_DELAY);
		// Write data back to the UART
		for(int i = 0; i< len; i++){
			send_data(data_buffer_ptr, len);
		}
	}
}

void init_uart(){
	//初始化串口
	uart_config_t uart_config = {
		.baud_rate = 74880,
		.data_bits = UART_DATA_8_BITS,
		.parity    = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE
	};
	uart_param_config(UART_NUM_0, &uart_config);
	uart_driver_install(UART_NUM_0, UART_BUF_SIZE * 2, 0, 0, NULL);

	// Configure a temporary buffer for the incoming data
	data_buffer_ptr = (uint8_t *) malloc(UART_BUF_SIZE);
}

void start_busi(){
	register_callback(conn_status_changed, process_message);
	init_tcp_conn();

	init_uart();
	loop_read_uart_forever();//这个方法不返回
}

