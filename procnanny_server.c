#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "memwatch.h"

/*********************** Server ***********************/

void killprevprocnanny( void );
int readconfigfile(char *cmdarg);
void handlesighup(int signum);
void handlesigint(int signum);
int read_from_client (int filedes);
int make_socket(uint16_t port);

uint16_t PORT =  0; // bind to any free port
int MAXMSG = 512;
  
char procname[128][255]; // for saving read from file
int numsecs[128];

// Signal flags
int hupflag = 1;
int hupmess = 0;
int inthandle = 0;

int main(int argc, char *argv[])
{
  mwInit();

  // kill any other procnannies
  killprevprocnanny();

  // Select variables
  int sock;
  fd_set active_fd_set, read_fd_set;
  int i;
  struct sockaddr_in clientname;
  size_t size;
  int count; // Number of programs in config file

  time_t currtime;
  // Log file
  char *logloc = getenv("PROCNANNYLOGS");
  FILE *LOGFILE = fopen(logloc, "w");

  // Server info
  char *infoloc = getenv("PROCNANNYSERVERINFO");
  FILE *INFOFILE = fopen(infoloc, "w");

  char name[128];
  gethostname(name, sizeof(name));

  time(&currtime);
  fprintf(INFOFILE, "[%.*s] procnanny server: PID %d on node %s, port %d\n", (int) strlen(ctime(&currtime))-1, ctime(&currtime), getpid(), name, PORT);
  fflush(INFOFILE);
  fclose(INFOFILE);

  time(&currtime);
  fprintf(LOGFILE, "[%.*s] Info: Parent process is PID %d\n", (int) strlen(ctime(&currtime))-1, ctime(&currtime), getpid());
  fflush(LOGFILE);

  signal(SIGHUP, handlesighup);
  signal(SIGINT, handlesigint);

  // Socket select
  // Code from http://www.gnu.org/software/libc/manual/html_node/Server-Example.html
  /* Create the socket and set it up to accept connections. */
  sock = make_socket (PORT);
  if (listen (sock, 1) < 0) {
    perror ("listen");
    exit (EXIT_FAILURE);
  }

  /* Initialize the set of active sockets. */
  FD_ZERO(&active_fd_set);
  FD_SET(sock, &active_fd_set);

  while (1) {

    // Read each line of config file, count is number of lines read (number of process names)
    if (hupflag == 1) {
      if (hupmess == 1) {
	time(&currtime);
	fprintf(LOGFILE, "[%.*s] Info: Caught SIGHUP.  Configuration file '%s' re-read.\n", (int) strlen(ctime(&currtime))-1, ctime(&currtime), argv[1]);
	fflush(LOGFILE);

	printf("[%.*s] Info: Caught SIGHUP.  Configuration file '%s' re-read.\n", (int) strlen(ctime(&currtime))-1, ctime(&currtime), argv[1]);
	hupmess = 0;
      }
      count = readconfigfile(argv[1]);
      hupflag = 0;
    }

    /* Block until input arrives on one or more active sockets. */
    read_fd_set = active_fd_set;
    if (select (FD_SETSIZE, &read_fd_set, NULL, NULL, NULL) < 0) {
      perror ("select");
      exit (EXIT_FAILURE);
    }

    /* Service all the sockets with input pending. */
    for (i = 0; i < FD_SETSIZE; ++i) {
      if (FD_ISSET (i, &read_fd_set)) {
	if (i == sock) {
	  /* Connection request on original socket. */
	  int new;
	  size = sizeof (clientname);
	  new = accept (sock,
			(struct sockaddr *) &clientname,
			&size);
	  if (new < 0) {
	      perror ("accept");
	      exit (EXIT_FAILURE);
	    }
	  fprintf (stderr,
		   "Server: connect from host %s, port %hd.\n",
		   name, PORT);
	  FD_SET (new, &active_fd_set);
	}
	else {
	  /* Data arriving on an already-connected socket. */
	  if (read_from_client (i) < 0) {
	      close (i);
	      FD_CLR (i, &active_fd_set);
	    }
	}
      }
    }
  }

  mwTerm();
  exit(0);
}


// Read in config file
int readconfigfile(char *cmdarg) {
  int count = 0;
  FILE *f = fopen(cmdarg, "r");
  /* Reading from conig file */
  while (fscanf(f, "%s %d", procname[count], &numsecs[count]) != EOF) {
    // Find PIDs for the program
    count++;
  }
  return count;
}

// Kill any previous instances of procnanny
void killprevprocnanny() {
  FILE *pni;
  pid_t procnanid;
  pid_t mypid = getpid();
  int killval;

  // Find PIDs for the program
  pni = popen("ps -C procnanny.server -o pid=", "r");
  
  while (fscanf(pni, "%d", &procnanid) != EOF) {
    if (procnanid != mypid) {
      killval = kill(procnanid, SIGKILL);
      if (killval != 0) {
	printf("Failed to kill old procnanny process.\n");
      }
    }
  }
  return;
}

void handlesighup(int signum) {
  hupflag = 1;
  hupmess = 1;
}

void handlesigint(int signum) {
  inthandle = 1;
}

// From http://www.gnu.org/software/libc/manual/html_node/Server-Example.html
int read_from_client (int filedes) {
  char buffer[MAXMSG];
  int nbytes;

  nbytes = read (filedes, buffer, MAXMSG);
  if (nbytes < 0)
    {
      /* Read error. */
      perror ("read");
      exit (EXIT_FAILURE);
    }
  else if (nbytes == 0)
    /* End-of-file. */
    return -1;
  else
    {
      /* Data read. */
      fprintf (stderr, "Server: got message: `%s'\n", buffer);
      return 0;
    }
}

// From http://www.gnu.org/software/libc/manual/html_node/Inet-Example.html#Inet-Example, BOB Beck, and Paul Lu
int make_socket(uint16_t port) {
   
  int sock;
  struct sockaddr_in sockaddr;

  /* Create the socket. */
  sock = socket (PF_INET, SOCK_STREAM, 0);
  if (sock < 0)
    {
      perror ("socket");
      exit (EXIT_FAILURE);
    }

  /* Give the socket a name. */
  sockaddr.sin_family = AF_INET;
  sockaddr.sin_port = htons(port);
  sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (sock == -1) {
    perror("socket failed");
  }
  if (bind (sock, (struct sockaddr *) &sockaddr, sizeof (sockaddr)) < 0) {
    perror ("bind");
    exit (EXIT_FAILURE);
  }

  return sock;
}


