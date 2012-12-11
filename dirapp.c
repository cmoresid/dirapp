/*
 * =====================================================================================
 *
 *       Filename:  dirapp.c
 *
 *    Description:  The main driver function that verifies initial parameters and starts
 *					either the server or client functionality based on the given
 *					parameters.
 *
 *        Version:  1.0
 *        Created:  24/10/2012 11:19:03
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Connor Moreside (conman720), cmoresid@ualberta.ca
 *   Organization:  CMPUT 379
 *
 * =====================================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>

#include "server.h"
#include "client.h"
#include "common.h"

int main(int argc, const char* argv[])
{
	if (argc == 1) {
		// Try to start client mode
		start_client();
	} else if (argc == 4) {
		// Try to start server mode
		int port_number, period;
		DIR* d;
		// Verify valid port number
		if ((port_number = atoi(argv[1])) <= 0)
			err_quit("Invalid port number.");
		if (port_number < 1024)
			err_quit("Cannot bind to well-known port (1-1024).");
		if (port_number > 65535)
			err_quit("Invalid port number.");
		// Check if directory is valid
		if ((d = opendir(argv[2])) == NULL) {
			err_quit("Cannot open directory.");
		} else {
			closedir(d);
		}
		// Check valid period
		if ((period = atoi(argv[3])) <= 0)
			err_quit("Invalid period.");
		if (!(period > 0 && period <= 255))
			err_quit("Period must be 0 < period <= 255");
		// Valid parameters, try to start server
		start_server(port_number, argv[2], period);
	} else {
		printf("Usage: dirapp [portnumber] [dirname] [period]\n");
		return 1;
	}

	return 0;
}

