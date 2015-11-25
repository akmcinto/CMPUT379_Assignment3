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
int make_socket(uint16_t port);
int getPortNumber( int socketNum );

uint16_t PORT =  0; // bind to any free port
int MAXMSG = 256;
  
char procname[128][255]; // for saving read from file
int numsecs[128];


int clients[36];
int nclients = 0;
fd_set active_fd_set;
FILE *LOGFILE;

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
  fd_set read_fd_set;
  struct sockaddr_in clientname;
  struct sockaddr_in sockname;
  socklen_t size;
  int count; // Number of programs in config file

  time_t currtime;
  // Log file
  char *logloc = getenv("PROCNANNYLOGS");
  LOGFILE = fopen(logloc, "w");

  // Server info
  char *infoloc = getenv("PROCNANNYSERVERINFO");
  FILE *INFOFILE = fopen(infoloc, "w");

  char name[128];
  gethostname(name, sizeof(name));

  signal(SIGHUP, handlesighup);
  signal(SIGINT, handlesigint);
  signal(SIGPIPE, SIG_IGN);

  count = readconfigfile(argv[1]);

  // Socket select
  /* Create the socket and set it up to accept connections. */
  memset(&sockname, 0, sizeof(sockname));
  sockname.sin_family = AF_INET;
  sockname.sin_port = htons(PORT);
  sockname.sin_addr.s_addr = htonl(INADDR_ANY);
  sock=socket(AF_INET,SOCK_STREAM | SOCK_NONBLOCK,0);

  if ( sock == -1)
    perror("socket failed");

  if (bind(sock, (struct sockaddr *) &sockname, sizeof(sockname)) == -1)
    perror("bind failed");

  if (listen(sock,36) == -1)
    perror("listen failed");

  printf("Server up and listening for connections on port %d\n",
	 getPortNumber( sock ) );

  time(&currtime);
  fprintf(LOGFILE, "[%.*s] procnanny server: PID %d on node %s, port %d\n", (int) strlen(ctime(&currtime))-1, ctime(&currtime), getpid(), name, getPortNumber(sock));
  fflush(LOGFILE);

  fprintf(INFOFILE, "NODE %s PID %d PORT %d\n", name, getpid(), getPortNumber(sock));
  fflush(INFOFILE);
  fclose(INFOFILE);


  /* Initialize the set of active sockets. */
  FD_ZERO(&active_fd_set);
  FD_SET(sock, &active_fd_set);

  //write_fd_set = active_fd_set;

  /* Block until input arrives on one or more active sockets. */
  int rewrite = 1;
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
      //rewrite = 1; 
      int h;
      for (h = 0; h < nclients; h++) {
	write(clients[h], &count, sizeof(count));
	int j;
	for (j = 0; j < count; j++) {
	  write(clients[h], &procname[j], 255);
	  write(clients[h], &numsecs[j], sizeof(int));
	}
      }
      hupflag = 0;
    }

    read_fd_set = active_fd_set;
    //write_fd_set = active_fd_set;

    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    if (select (FD_SETSIZE, &read_fd_set, NULL, NULL, &timeout) < 0) {
      timeout.tv_sec = 5;
      timeout.tv_usec = 0;

      continue;
    }

    /* Service all the sockets with input pending. */
    if (FD_ISSET (sock, &read_fd_set)) {
      /* Connection request on original socket. */
      size = sizeof (clientname);
      clients[nclients] = accept (sock, (struct sockaddr *) &clientname, &size);
      if (clients[nclients] < 0) {
	
      }
      FD_SET(clients[nclients], &active_fd_set);
      FD_SET(clients[nclients], &read_fd_set);
      //FD_SET(clients[nclients], &write_fd_set);
      rewrite = 1; 
      nclients++;
    }

    int h;
    if (rewrite) {
      for (h = 0; h < nclients; h++) {
	write(clients[h], &count, sizeof(count));
	int j;
	for (j = 0; j < count; j++) {
	  write(clients[h], &procname[j], 255);
	  write(clients[h], &numsecs[j], sizeof(int));
	}
      }
      rewrite = 0;
    }
    
    
    for(h = 0; h < nclients; h++) {
      
      if (FD_ISSET (clients[h], &read_fd_set)) {
	char buffer[MAXMSG];
	read (clients [h], buffer, MAXMSG);
	fprintf(LOGFILE, "%s", buffer);
	fflush(LOGFILE);
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
  int h;
  int killcount = 0;
  char nodes[1024];
  nodes[0] = ' ';
  for(h = 0; h < nclients; h++) {
    write(clients[h], "sigint", MAXMSG);
    write(clients[h], "sigint", MAXMSG);
    write(clients[h], "sigint", MAXMSG);
  }
  while (nclients > 0) {
    for(h = 0; h < nclients; h++) {
      char buffer[MAXMSG];
      int ret = read(clients[h], buffer, MAXMSG);
      if (ret == 0) {
	
      //} else {
	//fprintf(LOGFILE, "%s", buffer);
	//fflush(LOGFILE);
	int kills = 0;
	char node[255];
	sscanf(buffer, "%d %s", &kills, node);
	killcount += kills;
	sprintf(nodes, "%s%s ", nodes, node);

	FD_CLR(clients[h], &active_fd_set);
	nclients--;
	}
    }
  }

  time_t currtime;
  time(&currtime);
  printf("[%.*s] Info: Caught SIGINT.  Exiting cleanly. %d process(es) killed on%s.\n", (int) strlen(ctime(&currtime))-1, ctime(&currtime), killcount, nodes);

  fprintf(LOGFILE, "[%.*s] Info: Caught SIGINT.  Exiting cleanly. %d process(es) killed on%s\n", (int) strlen(ctime(&currtime))-1, ctime(&currtime), killcount, nodes);

  fflush(LOGFILE);
  fclose(LOGFILE);
  mwTerm();
  exit(0);
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

int getPortNumber( int socketNum )
{
  struct sockaddr_in addr;
  int rval;
  socklen_t addrLen;

  addrLen = (socklen_t)sizeof( addr );

  /* Use getsockname() to get the details about the socket */
  rval = getsockname( socketNum, (struct sockaddr*)&addr, &addrLen );
  if( rval != 0 )
    perror("getsockname() failed in getPortNumber()");

  /* Note cast and the use of ntohs() */
  return( (int) ntohs( addr.sin_port ) );
} /* getPortNumber */

