/*
 * =====================================================================================
 *
 *       Filename:  client.h
 *
 *    Description:  Contains definitions and function prototypes for the client
 *					functionality of dirapp.
 *
 *        Version:  1.0
 *        Created:  24/10/2012 11:29:40
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Connor Moreside (conman720), cmoresid@ualberta.ca
 *   Organization:  CMPUT 379
 *
 * =====================================================================================
 */

#ifndef CLIENT_H

#include <pthread.h>

#include "common.h"

#define SPACE				0x20		/* ASCII value for a space character */

#define ADD					"add"		/* String value for the add command */
#define REMOVE				"remove"	/* String value for the remove command */
#define LIST				"list"		/* String value for the list command */
#define QUIT				"quit"		/* String value for the quit command */

#define INVALID_C			'0'			/* Byte value for an invalid command */
#define ADD_SERVER_C		'1'			/* Byte value for the add server command */
#define REMOVE_SERVER_C		'2'			/* Byte value for the remove server command */
#define LIST_SERVERS_C		'3'			/* Byte value for the list servers command */
#define QUIT_C				'4'			/* Byte value for the quit command */

/* Macro function to check if the token matches a particular command */
#define CMD_CMP(TOK, CMD)	(strcmp(TOK, CMD) == 0)

/* Represents a server that a client is connected to */
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

/* A linked list that stores information pertaining to a connected server */
struct serverlist {
	struct server* head;
	struct server* tail;
	int count;
};

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  start_client()
 *  Description:  Starts the client functionality of dirapp and initializes the signal
 *				  thread.
 *	  Arguments:  None
 *        Locks:  io_lock : When start_client needs to write to stdout, it holds the
 *							io_lock so only 1 entity can write to it at a time.
 *      Returns:  0
 * =====================================================================================
 */
int start_client();

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  get_updates(int socketfd, int numdiffs)
 *  Description:  Retrieve updates from a given server socket
 *	  Arguments:  socketfd : The socket of the server to retrieve updates from
 *				  numdiffs : The number of updates to expect from the server
 *        Locks:  servers_lock : Ensure that the servers list is not altered while
 *								 updates are being received.
 *				  s_lock       : Don't allow a particular server reference to be removed
 *								 while an update is being received.
 *      Returns:  void
 * =====================================================================================
 */
void get_updates(int socketfd, int numdiffs);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  kill_servers(struct serverlist* servers, int pipe)
 *  Description:  Disconnect from all connected servers and remove each reference from
 *                servers.
 *	  Arguments:  pipe :  A pipe used to send the socket file descriptors of each
 *						 server to the main thread as to clear each socket from the
 *						 master file descriptor list
 *        Locks:  servers_lock : Ensure that the servers list is not altered while
 *								 updates are being received.
 *				  s_lock       : Don't allow a particular server reference to be removed
 *								 while an update is being received.
 *      Returns:  void
 * =====================================================================================
 */
void kill_servers(int pipe);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  disconnect_from_server(int socketfd, int pipe)
 *  Description:  Disconnect from a server, based on the given socket corresponding to
 *				  a server connection. Once the reference is removed, send the socket
 *				  through the pipe.
 *	  Arguments:  socketfd : The socket corresponding to the server reference to
 *							 remove.
 *				  pipe     : A pipe used to send the socket file descriptor of the
 *							 server to the main thread as to clear the socket from the
 *							 master file descriptor list.
 *        Locks:  None
 *      Returns:  0 if everything is ok, -1 if there is a messy disconnect.
 * =====================================================================================
 */
int disconnect_from_server(int socketfd, int pipe);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  signal_thread(void* arg)
 *  Description:  Thread that handles all signals
 *	  Arguments:  None
 *        Locks:  None
 *      Returns:  0
 * =====================================================================================
 */
static void* signal_thread(void* arg);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  handle_input(void* arg)
 *  Description:  Thread that handles all input from user (keyboard)
 *	  Arguments:  None
 *        Locks:  None
 *      Returns:  0
 * =====================================================================================
 */
void* handle_input(void* arg);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  init_server(struct thread_arg* targ)
 *  Description:  Thread that handles the setup of a new connection to a server
 *	  Arguments:  targ->buff : Contains the add command's arguments
 *				  targ->pipe : The pipe used to send the new socket back to the main
 *							   thread to add to the master fd list
 *        Locks:  io_lock : Write to stdout  
 *      Returns:  (void)
 * =====================================================================================
 */
void* init_server(void* arg);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name: remove_server(struct thread_arg* targ) 
 *  Description: Thread that removes a server connection based on its socket
 *	  Arguments: targ->buff : Contains the arguments of the remove command
 *				 targ->pipe : Contains the pipe fd used to write back the socket
 *							  of the server to remove from the master fd list
 *        Locks: io_lock 	  : Write to stdout
 *				 servers_lock : Ensure nobody else alters servers while removing 
 *      Returns:  (void)
 * =====================================================================================
 */
void* remove_server(void* arg);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  list_servers(struct serverlist* servers)
 *  Description:  Prints out all the connected servers specified in servers
 *	  Arguments:  
 *        Locks:  servers_lock : Ensure servers is not altered while reading list
 *      Returns:  (void)
 * =====================================================================================
 */
void list_servers();

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  add_server_ref(host, path, port, period, socketfd)
 *  Description:  Adds a new server reference to servers linked list, based on given 
 *                parameters 
 *	  Arguments:  host   : The host name of the server
 *				  path   : Path/name of the directory being monitored by the server
 *				  port   : Port number the server is listening on
 *				  period : The refresh period of the server
 *        Locks:  servers_lock : Ensure servers is not altered while adding a new server
 *								 to servers
 *      Returns:  (void)
 * =====================================================================================
 */
void add_server_ref(const char* host, const char* path, int port, int period, int socketfd);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  remove_server_ref(int socketfd)
 *  Description:  Removes a server reference based based on the given socket fd
 *	  Arguments:  socketfd : Socket file descriptor of server reference to remove
 *        Locks:  servers_lock : Ensure servers is not altered while removing a server
 *								 from the list
 *      Returns:  (void)
 * =====================================================================================
 */
void remove_server_ref(int socketfd);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  find_server_ref(int socketfd)
 *  Description:  Retrieves a server reference from servers based on the socket fd
 *	  Arguments:  socketfd : The socket fd of the server to retrieve
 *        Locks:  None
 *      Returns:  Reference to connected server or NULL if not found
 *		  Free?:  No
 * =====================================================================================
 */
struct server* find_server_ref(int socketfd);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  find_server_ref2(const char* host, int port)
 *  Description:  Finds a reference to a connected server based on the given host and
 *				  port number
 *	  Arguments:  host : Host name of the server
 *				  port : Port number of the server
 *        Locks:  None
 *      Returns:  Reference to connected server or NULL if not found
 *		  Free?:  No
 * =====================================================================================
 */
struct server* find_server_ref2(const char* host, int port);

#endif
