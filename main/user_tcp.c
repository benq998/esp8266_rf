#include "user_tcp.h"
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

static int sock;//socket handle
static tcp_status stat = tcp_disconn;//socket状态

static char magic_head[] = {(char)36, (char)50};

static char* hartbeat = "keep";//心跳数据，主要为了位置tcp连接持久
static TaskHandle_t socket_recv_thread_handle;//心跳定时器handler
static os_timer_t hart_beat_timer;//心跳定时器

#define max_buffer_length  256
static char buffer[max_buffer_length];//接受数据，协议解析缓冲区
static int buffer_data_length = 0;//缓冲区中已经有的数据长度

static os_timer_t reconn_later_timer;//延迟重连计时器

static int64_t last_hart_beat_time = 0;

void register_callback(tcp_status_callback scb, tcp_recv_callback rcb){
	status_cb = scb;
	recv_cb = rcb;
}

static void send_hart_beat_task(void *timer_arg){
	if(stat != tcp_connected){
		return;
	}
	//发送心跳数据
	if(sock >= 0){
		if(send_data(hartbeat, strlen(hartbeat)) < 0){
			//fail
			ESP_LOGI(TAG, "发送心跳失败");
		}else{
			//succ
			ESP_LOGI(TAG, "发送心跳成功");
		}
	}
}

static void connect_later_task(void *pv);

static void reconnect_later(){
	//稍后发起重连
	os_timer_setfn(&reconn_later_timer, connect_later_task, NULL);
	os_timer_arm(&reconn_later_timer, 2000, false);//循环触发
}

static void reconnect(){
	buffer_data_length = 0;
	stat = tcp_disconn;
	if(sock != 0){
		close(sock);
		sock = 0;
	}
	init_tcp_conn();
}

static void connect_later_task(void *pv){
	reconnect();
}

static void process_heart_beat(){
	last_hart_beat_time = currentTimeMillis();
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
	const int buf_len = 128;
	char buf[buf_len];
	while(true){
		//死循环
		if(stat != tcp_connected){
			//连接状态
			continue;
		}
		int n = recv(sock,buf,buf_len,0);
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
	memset(&sockaddr,0,sizeof(sockaddr));
	sockaddr.sin_family = AF_INET;
	sockaddr.sin_port = htons(server_port);
	inet_pton(AF_INET, server_ip, &sockaddr.sin_addr);

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if(sock < 0) {
		ESP_LOGE(TAG, "... Failed to allocate socket.");
		reconnect_later();
		return;
	}
	if(connect(sock, (struct sockaddr*)&sockaddr, sizeof(sockaddr)) != 0) {
		ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
		close(sock);
		reconnect_later();
		return;
	}
	status_cb(tcp_connected);
	if(socket_recv_thread_handle == 0){
		xTaskCreate(&socket_recv_thread, "socket_recv_thread", 16384, NULL, 5, &socket_recv_thread_handle);
	}
	if(hart_beat_timer.timer_func == NULL){
		//还没有设置计时器
		os_timer_setfn(&hart_beat_timer, send_hart_beat_task, NULL);
		os_timer_arm(&hart_beat_timer, 5000, true);//循环触发
	}
}

#define send_data_buffer_max 256

static char send_buffer[send_data_buffer_max];

//发送数据
int send_data(void* data, int length){
	if(stat == tcp_connected){
		if(length + 3 > send_data_buffer_max){
			//判断数据手否过长
			return -1;
		}
		send_buffer[0] = magic_head[0];
		send_buffer[1] = magic_head[1];//add magic header
		send_buffer[2] = length;//add length field
		memcpy(send_buffer + 3, data, length);//add data
		int written = (int)send(sock, send_buffer, length + 3, 0);
		if(written > 0){
			return written;
		}else{
			//异常或连接断开

		}
	}
	return -1;
}
