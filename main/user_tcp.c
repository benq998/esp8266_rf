#include "user_tcp.h"
#include "stdlib.h"
#include <string.h>
#include "sys/socket.h"
#include "esp_timer.h"
#include "esp_types.h"
#include "netdb.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "time.h"
#include "user_main.h"

static tcp_status_callback status_cb;
static tcp_recv_callback recv_cb;

static int sock = -1;//socket handle
static tcp_status stat = tcp_disconn;//socket状态

static char magic_head[] = {(char)0x36, (char)0x50};

static char* hartbeat = "keep";//心跳数据，主要为了位置tcp连接持久
static TaskHandle_t socket_recv_thread_handle;//接受线程任务handle
static TaskHandle_t socket_send_heart_bean_handle;//心跳任务handler

#define max_buffer_length  256
static char buffer[max_buffer_length];//接受数据，协议解析缓冲区
static int buffer_data_length = 0;//缓冲区中已经有的数据长度

static os_timer_t reconn_later_timer;//延迟重连计时器

static int last_hart_beat_time = 0;

void register_callback(tcp_status_callback scb, tcp_recv_callback rcb){
	status_cb = scb;
	recv_cb = rcb;
}

static void send_hart_beat(void *timer_arg){
//	ESP_LOGI(TAG, "send hart beat msg.");
	if(stat != tcp_connected){
//		ESP_LOGI(TAG, "发送心跳消息,因为没有连接，忽略此次.");
		return;
	}
	//发送心跳数据
	if(sock >= 0){
		int msgLen = strlen(hartbeat);
		char hbmsg[msgLen + 1];
		hbmsg[0] = 0;
		memcpy(hbmsg+1, hartbeat, msgLen);
		if(send_data(hbmsg, msgLen + 1) < 0){
			//fail
//			ESP_LOGI(TAG, "heart beat send fail.");
		}else{
			//succ
//			ESP_LOGI(TAG, "heart beat send succ.");
		}
	}
}

static void send_hart_beat_task(void *timer_arg){
	while(1){
		if(stat == tcp_connected){
			send_hart_beat(NULL);
		}
		vTaskDelay(5000 / portTICK_RATE_MS);
	}
}

static void connect_later_task(void *pv);

static void reconnect_later(){
	ESP_LOGI(TAG,"reconnect_later");
	stat = tcp_disconn;
	//稍后发起重连
	os_timer_setfn(&reconn_later_timer, connect_later_task, NULL);
	os_timer_arm(&reconn_later_timer, 2000, false);//循环触发
}

static void reconnect(){
	ESP_LOGI(TAG,"reconnect");
	buffer_data_length = 0;
	stat = tcp_disconn;
	if(sock != -1){
		close(sock);
		sock = -1;
	}
	init_tcp_conn();
}

static void connect_later_task(void *pv){
	reconnect();
}

static void process_heart_beat(){
	ESP_LOGI(TAG, "receive heart beat replay.");
	last_hart_beat_time = currentTimeSeconds();
}

//分析协议，如果协议数据完成，把数据冲到业务层处理,返回协议分析处理的字节数，>0标识正确分析并处理，=0标识数据不足，等下次处理，<1标识数据不正确，需要断开网络重新连接
static int parseProtocol(char *buf, int length){
	if(length < 3){
		//数据不足
		return 0;
	}
	if(buf[0] == magic_head[0] && buf[1] == magic_head[1]){
		//head 验证通过
		uint32_t dataLen = (uint32_t)buf[2];
		if(length >= dataLen + 3){
			//协议完整
			int type = buf[3];//协议类型
			if(type == 0){//类型0标识心跳信息
				//心跳数据
				process_heart_beat();
			}else{
				recv_cb(buf + 3, dataLen);
			}
			return dataLen + 3;
		}else{
			//数据不足
			return 0;
		}
	}
	//数据不对，触发重新连接
	return -1;
}

static void recv_data(char* buf, int len){
	if(buffer_data_length + len > max_buffer_length){
		//不应该有这么大的数据，应该断了连接重新链
		reconnect();
		return;
	}
	memcpy(buffer + buffer_data_length, buf, len);
	buffer_data_length += len;
	int rst = parseProtocol(buffer, buffer_data_length);
	if(rst > 0){
		//协议处理成功
		memcpy(buffer,buffer+rst,buffer_data_length - rst);//把缓冲区后面的数据诺的头部
		buffer_data_length -= rst;
	}else if(rst < 0){
		//协议处理失败，需要重新连接
		reconnect();
	}else{
		//数据不足，等下次在接受出数据处理
	}
}

static void socket_recv_thread(void *pvParameters){
	ESP_LOGI(TAG,"socket_recv_thread start");
	const int buf_len = 128;
	char buf[buf_len];
	while(1){
		//死循环
		if(stat != tcp_connected){
			//连接状态
			vTaskDelay(50 / portTICK_PERIOD_MS);
			continue;
		}
		int n = read(sock, buf, buf_len);
		ESP_LOGI(TAG,"receive %d bytes", n);
		if(n > 0){
			recv_data(buf, n);
		}else{
			//接受错误或连接中断
			status_cb(tcp_disconn);
			reconnect();
		}
	}
}

void init_tcp_conn(){
	struct sockaddr_in sockaddr;
	memset(&sockaddr, 0, sizeof(sockaddr));
	sockaddr.sin_family = AF_INET;
	sockaddr.sin_port = htons(server_port);
	inet_pton(AF_INET, server_ip, &sockaddr.sin_addr);
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if(sock < 0) {
		ESP_LOGI(TAG, "... Failed to allocate socket.");
		reconnect_later();
		return;
	}
	if(connect(sock, (struct sockaddr*)&sockaddr, sizeof(sockaddr)) != 0) {
		ESP_LOGI(TAG, "... socket connect failed errno=%d", errno);
		reconnect_later();
		return;
	}
	ESP_LOGI(TAG,"socket connect succ. socketfd:%d", sock);
	stat = tcp_connected;
	status_cb(tcp_connected);


	if(socket_recv_thread_handle == 0){
		xTaskCreate(&socket_recv_thread, "socket_recv_thread", 16384, NULL, 5, &socket_recv_thread_handle);
	}

	if(socket_send_heart_bean_handle == 0){
		xTaskCreate(&send_hart_beat_task, "send_hart_beat_task", 16384, NULL, 5, &socket_recv_thread_handle);
	}

}

#define send_data_buffer_max 256

static char send_buffer[send_data_buffer_max];

//发送数据
int send_data(char* data, int length){
	if(stat == tcp_connected){
//		ESP_LOGI(TAG,"send_data...len:%d", length);
		if(length + 3 > send_data_buffer_max){
//			ESP_LOGI(TAG,"send data too long ....");
			//判断数据手否过长
			return -1;
		}
		send_buffer[0] = magic_head[0];
		send_buffer[1] = magic_head[1];//add magic header
		send_buffer[2] = length;//add length field
		memcpy(send_buffer + 3, data, length);//add data
//		ESP_LOGI(TAG,"socket sending %d bytes.",(length + 3));
		int written = write(sock, send_buffer, (length + 3));
//		ESP_LOGI(TAG,"socket sent %d bytes", written);
		if(written > 0){
			return written;
		}else{
			//异常或连接断开

		}
	}else{
//		ESP_LOGI(TAG,"send_data, buy not have connection.");
	}
	return -1;
}
