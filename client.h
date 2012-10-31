/*
 * =====================================================================================
 *
 *       Filename:  client.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  24/10/2012 11:29:40
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Connor Moreside (conman720), cmoresid@ualberta.ca
 *   Organization:  
 *
 * =====================================================================================
 */

#ifndef CLIENT_H

#include <pthread.h>
#include "common.h"

#define READ_PIPE 			pipes[0]
#define WRITE_PIPE			pipes[1]
#define SPACE				0x20

#define ADD					"add"
#define REMOVE				"remove"
#define LIST				"list"
#define QUIT				"quit"

#define INVALID_C			'0'
#define ADD_SERVER_C		'1'
#define REMOVE_SERVER_C		'2'
#define LIST_SERVERS_C		'3'
#define QUIT_C				'4'

#define CMD_CMP(TOK, CMD)	(strcmp(TOK, CMD) == 0)

struct thread_arg {
	char* buff;
	int socket_pipe;
};

struct server {
	struct server* next;
	struct server* prev;
	int socket;
	int port;
	int period;
	char* host;
	char* path;
	pthread_mutex_t* s_lock;
};

struct serverlist {
	struct server* head;
	struct server* tail;
	int count;
};

void add_server_ref(const char* host, const char* path, int port, int period, int socketfd);
void remove_server_ref(int socketfd);
struct server* find_server_ref(int socketfd);
struct server* find_server_ref2(const char* host, int port);

int start_client();
int disconnect_from_server(int socketfd);
static void* signal_thread(void* arg);
void* handle_input(void* arg);

void* init_server(void* arg);
void* remove_server(void* arg);
void* list_servers(void* arg);

#endif // CLIENT_H
