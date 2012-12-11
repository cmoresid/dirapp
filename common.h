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
	#define SLEEP_TIME      3                       /* Give ample time for input during tests */
#else
	#define SLEEP_TIME      1                       /* For humans */
#endif

#define INIT_CLIENT1    0xFE            /* Initiates connection.    */
#define INIT_CLIENT2    0xED            /* Acknowledge connection.  */

#define REQ_REMOVE1     0xDE            /* Client requests removal from server. */
#define REQ_REMOVE2             0xAD            /* Server acknowledges client's removal request. */
#define NO_UPDATES              0x00            /* No updates to send to clients. */
#define END_COM                 0xFF            /* Ends communication. */
#define GOOD_BYE                "Goodbye"       /* Goodbye! */

#define MAX_CLIENTS     10                      /* Max number of clients a server talk with */
#define MAX_SERVERS     5               /* Max number of servers a client can talk to. */

#define BUFF_MAX        256

#ifndef PATH_MAX                                        /* Defined in Linux, don't overwrite otherwise */
	#define PATH_MAX    256         /* Max path size. */
#endif

#define MAX_FILENAME    256                     /* Max number of characters in filename */

/* Used to pass multiple parameters to a thread */
struct thread_arg {
	char* buff;
	int socket;
	int pipe;
	int period;
};

typedef unsigned char byte;                     /* Defines a byte (0-255). */

/*
 * ===  FUNCTION  ======================================================================
 *         Name:  err_quit(const char* error)
 *  Description:  Prints the message to stderr, and then exit with 1
 *	  Arguments:  error : The error message to print out
 *        Locks:  None
 *      Returns:  (void)
 * =====================================================================================
 */
void err_quit(const char* error);

/*
 * ===  FUNCTION  ======================================================================
 *         Name:  read_string(int socketfd, byte* buff, int buff_size)
 *  Description:  First reads in the length of the string from socketfd, and then
 *				  reads in the string and copies it into the provided buffer. The
 *				  string is then null terminated in the buffer.
 *	  Arguments:  socketfd  : The socket which to retrieve the string from
 *				  buff      : Where to store the read in string
 *				  buff_size : Size of the buffer
 *        Locks:  None
 *      Returns:  Length of string read in (not including null terminator) or -1 if
 *				  error has occured
 * =====================================================================================
 */
int read_string(int socketfd, byte* buff, int buff_size);

/*
 * ===  FUNCTION  ======================================================================
 *         Name:  read_byte(int socketfd)
 *  Description:  Reads in a byte from the given socket
 *	  Arguments:  socketfd : The socket which to read in a byte from
 *        Locks:  None
 *      Returns:  The byte that was read in or 0 if nothing was read
 * =====================================================================================
 */
byte read_byte(int socketfd);

/*
 * ===  FUNCTION  ======================================================================
 *         Name:  send_string(int socketfd, const char* str)
 *  Description:  Sends a string based on the prescribed protocol
 *	  Arguments:  socketfd : The socket to send the string to
 *				  str      : The string that is to be sent
 *        Locks:  None
 *      Returns:  The length of the string that was sent or -1 on error
 * =====================================================================================
 */
int send_string(int socketfd, const char* str);

/*
 * ===  FUNCTION  ======================================================================
 *         Name:  send_byte(int socketfd, byte b)
 *  Description:  Sends a single byte to the specified socket
 *	  Arguments:  socketfd : The socket to send the byte to
 *				  b        : The byte to send
 *        Locks:  None
 *      Returns:  1 if ok, -1 on error
 * =====================================================================================
 */
int send_byte(int socketfd, byte b);
#endif  // DIRAPP_H
