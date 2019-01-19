#ifndef __ESP_USER_TCP_H__
#define __ESP_USER_TCP_H__

#include "user_main.h"

typedef enum{
	tcp_connected=0,
	tcp_disconn
}tcp_status;

typedef void (*tcp_status_callback)(tcp_status);
typedef void (*tcp_recv_callback)(char* data,int length);

void register_callback(tcp_status_callback, tcp_recv_callback);

void init_tcp_conn();

int send_data(char* data, int length);

#endif
