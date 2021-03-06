/*
 * =====================================================================================
 *
 *       Filename:  common.c
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  24/10/2012 12:07:21
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Connor Moreside (conman720), cmoresid@ualberta.ca
 *   Organization:
 *
 * =====================================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "common.h"

void err_quit(const char* error)
{
	fprintf(stderr, "Error: %s\n", error);
	exit(1);
}

byte read_byte(int socketfd)
{
	size_t nbytes;
	byte buff[1];

	// Read in one byte
	if ((nbytes = recv(socketfd, buff, 1, 0)) <= 0) {
		return 0;
	} else {
		return buff[0];
	}
}

int read_string(int socketfd, byte* buff, int buff_size)
{
	size_t nbytes;
	size_t len;

	// Read in the size of the string
	if ((len = read_byte(socketfd)) <= 0) {
		return -1;
	}

	// If length of string exceeds the size of the buffer, return
	// error
	if (len >= buff_size) {
		return -1;
	}

	// Read in the string itself
	if ((nbytes = recv(socketfd, buff, len, MSG_WAITALL)) <= 0) {
		if (nbytes == 0) {
			return 0;
		} else {
			return -1;
		}
	} else {
		// Make it a string now
		buff[len] = '\0';
		return len;
	}
}

int send_byte(int socketfd, byte b)
{
	byte buff[1];
	buff[0] = b;

	if (send(socketfd, buff, 1, 0) <= 0) {
		return -1;
	}

	return 1;
}

int send_string(int socketfd, const char* str)
{
	byte len;
	len = strlen(str);
	byte byte_str[len];

	// Send the length of the string
	if (send_byte(socketfd, len) != 1) {
		return -1;
	}

	// Now send the string itself
	strncpy(byte_str, str, len);
	if (send(socketfd, byte_str, len, 0) <= 0) {
		return -1;
	}

	return len;
}

