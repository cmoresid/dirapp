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

int difference_direntrylist();
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
 *	  Arguments:  arg : The 'period' variable
 *        Locks:  clients_lock : Ensure clients is not changed while sending out
 *                               updates
 *
 *      Returns:  void
 * =====================================================================================
 */
void* send_updates(void* arg);

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
 *      Returns:  void
 * =====================================================================================
 */
static void* signal_thread(void* arg);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  init_client(void* arg)
 *  Description:  Handles a client on a separate thread.
 *	  Arguments:  arg : Represents the socket of the new client
 *        Locks:  clients_lock : Will be locked through a call to add_client_ref(...)
 *      Returns:  void
 * =====================================================================================
 */
void* init_client(void* arg);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  remove_client(void* arg)
 *  Description:  Stops sending updates to a client
 *	  Arguments:  arg : Represents the socket of the client to remove
 *        Locks:  clients_lock : Will be locked through a call to remove_client_ref(...)
 *      Returns:  void
 * =====================================================================================
 */
void* remove_client(void* arg);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  add_client_ref(int socketfd)
 *  Description:  Adds new client to a list of clients.
 *	  Arguments:  socketfd: The socket used to identify the client
 *        Locks:  clients_lock : Ensures that only one thread can modify 'clients'
 *      Returns:  void
 * =====================================================================================
 */
void add_client_ref(int socketfd);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  remove_client_ref(int socketfd)
 *  Description:  Removes a client from a list of clients.
 *	  Arguments:  socketfd: The socket used to identify the client
 *        Locks:  clients_lock : Ensures that only one thread can modify 'clients'
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
 *      Returns:  A pointer that represents 
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
 *      Returns:  1 on success, 0 on fail
 * =====================================================================================
 */
int add_direntry(struct direntrylist* list, struct direntry* entry);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  remove_direntry(struct direntrylist* list, struct direntry* entry)
 *  Description:  Removes a direntry in list if found
 *    Arguments:  list  : Directory list
 *				  entry : Entry to remove from the directory list
 *        Locks:  None
 *      Returns:  1 on success, 0 on fail
 * =====================================================================================
 */
int remove_direntry(struct direntrylist* list, struct direntry* entry);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  replace_direntry(struct direntrylist* list, 
 *                                 struct direntry* inlist, 
 *                                 struct direntry* replace)
 *  Description:  Replaces a directory entry in a directory list with another entry
 *    Arguments:  list        : The list of directory entries
 *                inlist      : Entry in list to replace
 *                replacewith : Entry to replace inlist with
 *        Locks:  None
 *      Returns:  1 on success, 0 on fail
 * =====================================================================================
 */
int replace_direntry(struct direntrylist* list,
        struct direntry* inlist,
        struct direntry* replacewith);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  exploredir(const char* path)
 *  Description:  Explores the given directory.
 *    Arguments:  path : The name/path of directory to explore.
 *        Locks:  None
 *      Returns:  A pointer to a directory entry list with the most up-to-date info
 *        Free?:  Yes
 * =====================================================================================
 */
int exploredir(struct direntrylist* list, const char* path);

#endif
