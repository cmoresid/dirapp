/*
 * =====================================================================================
 *
 *       Filename:  client.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  24/10/2012 11:30:13
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Connor Moreside (conman720), cmoresid@ualberta.ca
 *   Organization:  
 *
 * =====================================================================================
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <netinet/in.h>
#include <unistd.h>

#include "client.h"
#include "common.h"

int start_client() {
    printf("Start client!\n");
    
    int socketfd;
    struct sockaddr_in server;
    byte buff[256];
	
	byte init_client1;
	byte init_client2;
	byte period;
	size_t len;

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr("127.0.0.1");
    server.sin_port = htons(2222);

    if ( (socketfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
        printf("Could not create socket.\n");
        exit(1);
    }

    if (connect(socketfd, (struct sockaddr*)&server, sizeof(struct sockaddr_in)) < 0) {
        perror("connect");
        exit(1);
    }

	if ( (init_client1 = read_byte(socketfd)) <= 0 ) {
		printf("Could not read byte.\n");
		exit(1);
	} else {
		printf("Received: 0x%x\n", init_client1);
	}
	
	if ( (init_client2 = read_byte(socketfd)) <= 0 ) { 
		printf("Could not read byte.\n");
		exit(1);
	} else {
		printf("Received: 0x%x\n", init_client2);
	}
	
	len = read_string(socketfd, buff, 256);
	byte path[len];
	
	if (len <= 0) {
		printf("Cannot read in string.\n");
		exit(1);
	} else {
		strcpy(path, buff);
		printf("%s\n", buff);
	}


	if ( (period = read_byte(socketfd)) <= 0 ) {
		printf("Cannot read period.\n");
		exit(1);
	} else {
		printf("Period: %d\n", period);
	}

	
	if (read_string(socketfd, buff, 256) != 6) {
		printf("Could not read in update.\n");
		exit(1);
	} else {
		printf("%s\n", buff);
	}
	
	disconnect_from_server(socketfd);

    return 0;
}

int disconnect_from_server(int socketfd) {
	byte buff[8];
	
	if (send_byte(socketfd, REQ_REMOVE1) != 1) {
		fprintf(stderr, "Could not send byte.\n");
		shutdown(socketfd, SHUT_RDWR);
		return -1;
	}
	
	if (send_byte(socketfd, REQ_REMOVE2) != 1) {
		fprintf(stderr, "Could not send byte.\n");
		shutdown(socketfd, SHUT_RDWR);
		return -1;
	}
	
	if (read_byte(socketfd) != 0xFF) {
		fprintf(stderr, "Non-orderly shutdown.\n");
		shutdown(socketfd, SHUT_RDWR);
		return -1;
	}
	
	if (read_string(socketfd, buff, 8) != 7) {
		fprintf(stderr, "Non-orderly shutdown.\n");
		shutdown(socketfd, SHUT_RDWR);
		return -1;
	}
	
	printf("%s\n", buff);
	close(socketfd);
	
	return 0;
}
