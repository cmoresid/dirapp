/*
 * =====================================================================================
 *
 *       Filename:  dirapp.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  22/10/2012 15:42:08
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Connor Moreside (conman720), cmoresid@ualberta.ca
 *   Organization:  CMPUT379
 *
 * =====================================================================================
 */

#ifndef DIRAPP_H
#define DIRAPP_H

#ifdef TESTS
	#define SLEEP_TIME	5			/* Give ample time for input during tests */
#else
	#define SLEEP_TIME	1			/* For humans */
#endif

#define	INIT_CLIENT1    0xFE	 	/* Initiates connection.    */
#define	INIT_CLIENT2    0xED	 	/* Acknowledge connection.  */

#define	REQ_REMOVE1     0xDE     	/* Client requests removal from server. */	
#define	REQ_REMOVE2		0xAD	 	/* Server acknowledges client's removal request. */
#define	NO_UPDATES		0x00 		/* No updates to send to clients. */
#define END_COM			0xFF	 	/* Ends communication. */
#define GOOD_BYE		"Goodbye"	/* Goodbye! */

#define	MAX_CLIENTS     10		 	/* Max number of clients a server talk with */
#define MAX_SERVERS     5        	/* Max number of servers a client can talk to. */

#define BUFF_MAX        256

#ifndef PATH_MAX					/* Defined in Linux, don't overwrite otherwise */
	#define PATH_MAX    256      	/* Max path size. */
#endif

#define MAX_FILENAME	256			/* Max number of characters in filename */

struct thread_arg {
	char* buff;
	int socket;
	int pipe;
	int period;
};

typedef unsigned char byte;			/* Defines a byte (0-255). */

void err_quit(const char* error);
int read_string(int socketfd, byte* buff, int buff_size);
byte read_byte(int socketfd);
int send_string(int socketfd, const char* str);
int send_byte(int socketfd, byte b);

#endif  // DIRAPP_H
