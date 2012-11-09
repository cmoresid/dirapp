/*
 * =====================================================================================
 *
 *       Filename:  server.h
 *
 *    Description:  Holds definitions for the server functionality of dirapp
 *
 *        Version:  1.0
 *        Created:  24/10/2012 11:23:20
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Connor Moreside (conman720), cmoresid@ualberta.ca
 *   Organization:  CMPUT 379
 *
 * =====================================================================================
 */

#ifndef SERVER_H
#define SERVER_H

#include <pthread.h>
#include <sys/stat.h>

#include "common.h"

#define PERM				0
#define UID					1
#define GID					2
#define SIZE				3
#define LAT					4
#define LMT					5
#define LFST				6
#define ADDED				7
#define REMOVED				8
#define MODIFIED			9
#define CHECKED				10

#define CLR_MASK(mask)		(mask = 0)

#define SET_PERM(mask)		(mask ^= (1 << PERM))
#define SET_UID(mask)		(mask ^= (1 << UID))
#define SET_GID(mask)		(mask ^= (1 << GID))
#define SET_SIZE(mask)		(mask ^= (1 << SIZE))
#define SET_LAT(mask)		(mask ^= (1 << LAT))
#define SET_LMT(mask)		(mask ^= (1 << LMT))
#define SET_LFST(mask)		(mask ^= (1 << LFST))
#define SET_ADDED(mask)		(mask ^= (1 << ADDED))
#define SET_REMOVED(mask)	(mask ^= (1 << REMOVED))
#define SET_MODIFIED(mask)	(mask |= (1 << MODIFIED))
#define SET_CHECKED(mask)	(mask ^= (1 << CHECKED))

#define IS_PERM(mask)		(mask & (1 << PERM))
#define IS_UID(mask)		(mask & (1 << UID))
#define IS_GID(mask)		(mask & (1 << GID))
#define IS_SIZE(mask)		(mask & (1 << SIZE))
#define IS_LAT(mask)		(mask & (1 << LAT))
#define IS_LMT(mask)		(mask & (1 << LMT))
#define IS_LFST(mask)		(mask & (1 << LFST))
#define IS_ADDED(mask)		(mask & (1 << ADDED))
#define IS_REMOVED(mask)	(mask & (1 << REMOVED))
#define IS_MODIFIED(mask)	(mask & (1 << MODIFIED))
#define IS_CHECKED(mask)	(mask & (1 << CHECKED))

/* Ensure mutual exclusion for clients (defined in server.c) */
extern pthread_mutex_t clients_lock;

/* Contains information about connected clients. */
struct client {
    struct client* next;
	struct client* prev;
    int socket;
	pthread_mutex_t* c_lock;
};

/* Linked list of connected clients. */
struct clientlist {
	struct client* head;
	struct client* tail;
	int count;
};

/* Contains information about directory items. */
struct direntry {
	int mask;
    char filename[MAX_FILENAME];
    struct stat attrs;
    struct direntry* next;
};

/* Linked list of directory items. */
struct direntrylist {
    int count;
    struct direntry* head;
    struct direntry* tail;
};

void kill_clients(int pipe, const char* message);
int disconnect_from_client(int socket, int pipe);


/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  create_daemon(const char* name)
 *  Description:  Turns the current running process into a daemon
 *	  Arguments:  name : The name used to identify the daemon.
 *        Locks:  None
 *      Returns:  void
 * =====================================================================================
 */
void create_daemon(const char* name);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  send_updates(void* arg)
 *  Description:  Sends updates (if available) to any connected clients.
 *	  Arguments:  None
 *        Locks:  clients_lock : Ensure clients is not changed while sending out
 *                               updates
 *				  c_lock       : Aquires lock to a client when sending updates, so
 *								 client cannot be removed until update has been fully
 *								 sent
 *
 *      Returns:  (void)
 * =====================================================================================
 */
void* send_updates(void* arg);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  send_error(int socket, const char* err_msg)
 *  Description:  Sends err_msg to the connected client based on the socket fd
 *	  Arguments:  socket  : The socket of the connected client to send the error to
 *				  err_msg : The error message to send to the client
 *        Locks:  c_lock  : Aquires lock to a client when sending an error, so
 *						    client cannot be removed and other updates have been sent
 *                          prior to sending error
 *      Returns:  1 if no errors, -1 on error
 * =====================================================================================
 */
int send_error(int socket, const char* err_msg);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  send_error2(int socket, const char* err_msg)
 *  Description:  Sends err_msg to a client that has NOT been added to clients. Mainly
 *				  used to refuse a connection to a potential client
 *	  Arguments:  socket  : The socket of the connected client to send the error to
 *				  err_msg : The error message to send to the client
 *      Returns:  1 if no errors, -1 on error
 * =====================================================================================
 */
int send_error(int socket, const char* err_msg);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  start_server(int port_number, const char* dir_name, int period)
 *  Description:  Starts the server functionality of dirapp.
 *	  Arguments:  port_number : The port number which to bind the server to
 * 				  dir_name    : The name/path of directory to monitor
 *				  period      : Time interval to check for updates
 *        Locks:  None
 *      Returns:  0
 * =====================================================================================
 */
int start_server(int port_number, const char* dir_name, int period);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  signal_thread(void* arg)
 *  Description:  Used to spawn a thread that will handle any incoming signal
 *	  Arguments:  arg : The 'period' variable
 *        Locks:  None
 *      Returns:  (void)
 * =====================================================================================
 */
static void* signal_thread(void* arg);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  init_client(int socket)
 *  Description:  Handles a client on a separate thread.
 *	  Arguments:  socket : Represents the socket of the new client
 *        Locks:  clients_lock : Will be locked through a call to add_client_ref(...)
 *      Returns:  (void)
 * =====================================================================================
 */
void* init_client(void* arg);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  remove_client(void* arg)
 *  Description:  Stops sending updates to a client
 *	  Arguments:  arg : Represents the socket of the client to remove
 *        Locks:  clients_lock : Make sure clients is not altered while trying to
 *				  remove a client
 *      Returns:  (void)
 * =====================================================================================
 */
void* remove_client(void* arg);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  add_client_ref(int socketfd)
 *  Description:  Adds new client to a list of clients.
 *	  Arguments:  socketfd: The socket used to identify the client
 *        Locks:  None
 *      Returns:  (void)
 * =====================================================================================
 */
void add_client_ref(int socketfd);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  remove_client_ref(int socketfd)
 *  Description:  Removes a client from a list of clients.
 *	  Arguments:  socketfd: The socket used to identify the client
 *        Locks:  c_lock : Ensure client is not currently receiving updates before
 *						   removing
 *      Returns:  void
 * =====================================================================================
 */
void remove_client_ref(int socketfd);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  find_client_ref(int socketfd)
 *  Description:  Retrieves a pointer to the partiular client
 *	  Arguments:  socketfd: The socket used to identify the client
 *        Locks:  None
 *      Returns:  A pointer that represents the client given by the socket
 * 		  Free?:  No  
 * =====================================================================================
 */
struct client* find_client_ref(int socketfd);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  init_direntrylist()
 *  Description:  Initializes and returns a new direntrylist
 *	  Arguments:  None
 *        Locks:  None
 *      Returns:  A pointer that represents a list of entries within a directory or
 *				  NULL if memory could not be allocated
 * 		  Free?:  Yes
 * =====================================================================================
 */
struct direntrylist* init_direntrylist();

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  add_direntry(struct direntrylist* list, struct direntry* entry)
 *  Description:  Adds a direntry to a given direntrylist
 *    Arguments:  list  : Directory list
 *				  entry : Entry to add from the directory list
 *        Locks:  None
 *      Returns:  1 on success, -1 on fail
 * =====================================================================================
 */
int add_direntry(struct direntrylist* list, struct direntry* entry);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  reuse_direntrylist(struct direntrylist* list)
 *  Description:  Removes all direntries in the given list, so the list can be reused
 *    Arguments:  list  : A directory list to recycle
 *        Locks:  None
 *      Returns:  (void)
 * =====================================================================================
 */
void reuse_direntrylist(struct direntrylist* list);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  find_direntry(struct direntrylist* list, struct direntry* entry)
 *  Description:  Finds the corresponding entry with same inode id in the specified 
 *                direntrylist instance
 *    Arguments:  list  : A directory list to search through
 *				  entry : The entry to find in list
 *        Locks:  None
 *      Returns:  The reference to the direntry with same ionode as entry or NULL if
 *				  not found
 * =====================================================================================
 */
struct direntry* find_direntry(struct direntrylist* list, struct direntry* entry);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  exploredir(struct direntrylist* list, const char* path)
 *  Description:  Builds a direntrylist with the name and attributes of all files in
 *				  the directory specified by path
 *    Arguments:  list : Store the results of the exploration in here
 *				  path : The name/path of directory to explore
 *        Locks:  None
 *      Returns:  A pointer to a directory entry list with the most up-to-date info
 *        Free?:  Yes
 * =====================================================================================
 */
int exploredir(struct direntrylist* list, const char* path);


/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  difference_direntrylist()
 *  Description:  Finds all the differences in the monitored directory by setting a
 *				  bit mask associated with each file entry with all the differences
 *				  found
 *    Arguments:  None
 *        Locks:  None
 *      Returns:  The number of differences found in the monitored directory
 * =====================================================================================
 */
int difference_direntrylist();

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  append_diff(buff, mode, filename, desc)
 *  Description:  Appends the mode, filename, and description of an update to buff
 *    Arguments:  list : Store the results of the exploration in here
 *				  path : The name/path of directory to explore
 *        Locks:  None
 *      Returns:  A pointer to a directory entry list with the most up-to-date info
 *        Free?:  Yes
 * =====================================================================================
 */
void append_diff(byte* buff, const char* mode, const char* filename, const char* desc);

#endif
