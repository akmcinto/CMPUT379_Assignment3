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
#include <netdb.h>
#include "memwatch.h"

/*********************** Client ***********************/

void killprevprocnanny( void );
void runmonitoring();
void forkfunc(pid_t procid, int numsecs, int pipefd[2], int returnpipefd[2]);
int readconfigfile(char *cmdarg);
int getpids(char procname[255], int index, int sock);
void die(int pipefds[128][2], int returnpipefds[128][2], int killcount, int sock);

int MAXMSG = 256;
uint16_t MYPORT = 2692; // bind to any free port
int serverport;
char *servername;

// Global variable declarations
pid_t childpids[128]; // Save pids of all existing child processes
int childcount; // number of children currently monitoring a process

char procname[255]; // for saving read from file
int numsecs;

pid_t procid[128]; // pids corresonding to each proces in config file

char alreadyreported[128][255];  // Saved names of programs already reported to not have any processes
  
size_t returnmesssize = sizeof(char)*7;  // Size of message child pipes to parent


int main(int argc, char *argv[])
{
  mwInit();

  // kill any other procnannies
  killprevprocnanny();

  // Get server info from input
  servername = argv[1];
  serverport = atoi(argv[2]);

  // Set up monitoring
  runmonitoring();

  exit(0);
}

// Read config file, monitor pids, handle signals, print info to log file
void runmonitoring() {
  
  char procnamesforlog[128][255];  // Array for holding each line of the file read in
  int numsecsperprocess[128]; // Will contain amounts of time per pid, not just process name
  pid_t allprocids[128]; // Processes children are monitoring  
  int freeindices[128]; // array of indices of children in childpids that are not monitoring any process
  int freeindex = -1; // End of the freeindices array.  -1 means no free children.
  time_t currtime; // Time for log files
  int killcount = 0; // Total processes killed for final log output
  // Array for pipes
  int pipefds[128][2];
  int returnpipefds[128][2];
  ssize_t main_readreturn;
  char rmessage[returnmesssize];
  char sockmess[MAXMSG];

  struct sockaddr_in server;
  struct hostent *host;

  host = gethostbyname(servername);

  if (host == NULL) {
    perror ("Producer: cannot get host description");
    exit (1);
  }

  int sock = socket(AF_INET, SOCK_STREAM, 0);

  if (sock < 0) {
    perror ("Client: cannot open socket");
    exit (1);
  }
  bzero(&server, sizeof(server));
  bcopy(host->h_addr, & (server.sin_addr), host->h_length);
  server.sin_family = host->h_addrtype;
  server.sin_port = htons(serverport);

  if (connect (sock, (struct sockaddr*) & server, sizeof (server))) {
    perror ("Producer: cannot connect to server");
    exit (1);
  }

  childcount = 0;

  // Counter variables
  int i, j, k;
  int count; // number of process names
  int pidcount;

  while (1) {

    for (i = 0; i < childcount; i++) {
      main_readreturn = read(returnpipefds[i][0], rmessage, returnmesssize);
      if (main_readreturn == -1) {
	// No message yet

      } else if (main_readreturn > 0) {
	if (strcmp(rmessage, "killed\n") == 0) {
	  killcount++;
	  // Write message to logfile 
	  time(&currtime);
	  sprintf(sockmess, "[%.*s] Action: PID %d (%s) killed after exceeding %d seconds.\n", (int) strlen(ctime(&currtime))-1, ctime(&currtime), allprocids[i], procnamesforlog[i], numsecsperprocess[i]);
	  
	  write(sock, &sockmess, MAXMSG);
	}
	// Add child pid to list of available ones
	freeindex++;
	freeindices[freeindex] = i;
	// Remove child pid from list of running ones
	allprocids[i] = 0;
      } else {
	// Pipe closed
      }
    }

    // Get number of processes off of socket
    while (count == 0) {    
      read(sock, &count, sizeof(int));
    }
    for (k = 0; k < count; k++) { // names
      read(sock, &procname, sizeof(procname));
      read(sock, &numsecs, sizeof(int));

      if (strcmp(procname, "kill") == 0) {
	die(pipefds, returnpipefds, killcount, sock);
      }

      // Get corrensponding pids
      pidcount = getpids(procname, k, sock);

      for (j = 0; j < pidcount; j++) { // pids
	int monitored = 0;
	int m;
	// Check if already monitored
	for (m = 0; m < childcount; m++) {
	  if (allprocids[m] == procid[j]) {
	    monitored = 1;
	  }
	}
      
	if (monitored == 0) { // if not monitored
	  // Initialize monitoring in log file
	  time(&currtime);

	  sprintf(sockmess, "[%.*s] Info: Initializing monitoring of process %s (PID %d).\n", (int) strlen(ctime(&currtime))-1, ctime(&currtime), procname, procid[j]);

	  write(sock, &sockmess, MAXMSG);

	  if (freeindex > -1) {
	    allprocids[freeindices[freeindex]] = procid[j];
	    memcpy(procnamesforlog[freeindices[freeindex]], procname, 255);
	    numsecsperprocess[freeindices[freeindex]] = numsecs;

	    write(pipefds[freeindices[freeindex]][1], &allprocids[freeindices[freeindex]], sizeof(pid_t)); 
	    write(pipefds[freeindices[freeindex]][1], &numsecsperprocess[freeindices[freeindex]], sizeof(int));

	    freeindex--;
	  } else {
	    
	    if (pipe(pipefds[childcount]) < 0) { // Open pipe for parent to write to child - write to 1, read from 0
	      printf("Pipe error!");
	    }   
	    
	    if (pipe(returnpipefds[childcount]) < 0) { // Set pipe for child to write to parent
	      printf("Pipe error!");
	    }  else {
	      fcntl(*returnpipefds[childcount], F_SETFL, O_NONBLOCK); // Set pipe to not block on read
	    }   
	    memcpy(procnamesforlog[childcount], procname, 255);  
	    numsecsperprocess[childcount] = numsecs;    
	    allprocids[childcount] = procid[j];

	    forkfunc(allprocids[childcount], numsecsperprocess[childcount], 
		     pipefds[childcount], returnpipefds[childcount]);
	    childcount++;
	  }	  
	}
      }
    }

    sleep(5);

  }
}

void die(int pipefds[128][2], int returnpipefds[128][2], int killcount, int sock) {
  time_t currtime;  
  char sockmess[MAXMSG];
  int o;
  for (o = 0; o < childcount; o++) {
    kill(childpids[o], SIGKILL);
    close(pipefds[childcount][0]);
    close(pipefds[childcount][1]);
    close(returnpipefds[childcount][0]);
    close(returnpipefds[childcount][1]);
  }

  time(&currtime);
  sprintf(sockmess, "[%.*s] Info: Caught SIGINT.  Exiting cleanly. %d process(es) killed.\n", (int) strlen(ctime(&currtime))-1, ctime(&currtime), killcount);

  write(sock, &sockmess, MAXMSG);
  close(sock);

  printf("[%.*s] Info: Caught SIGINT.  Exiting cleanly. %d process(es) killed.\n", (int) strlen(ctime(&currtime))-1, ctime(&currtime), killcount);

  mwTerm();
  exit(0);
}

// Function handling forking code, child process logic
void forkfunc(pid_t procid, int numsecs, int pipefd[2], int returnpipefd[2]) {
  pid_t pid;

  if ((pid = fork()) < 0) {
    printf("fork() error!");
  } 
  else if (pid == 0) {  // Child process
    while (1) {
      // Wait for amount of time
      sleep(numsecs);
	
      // Kill monitored process
      int killed = kill(procid, SIGKILL);
      // If the process is actually killed, print to log
      if (killed == 0) {
	// Write to pipe then wait for a read
	write(returnpipefd[1], "killed\n", returnmesssize); 
      }
      else {
	write(returnpipefd[1], "nokill\n", returnmesssize); 
      }
      
      procid = 0;

      read(pipefd[0], &procid, sizeof(pid_t));
      read(pipefd[0], &numsecs, sizeof(int));
    }
  } 
  else { // parent process
    childpids[childcount] = pid;
  } 
}

// For each process name, get associated pids
int getpids(char procname[255], int index, int sock) {
  char cmdline[269]; // for creating pgrep command (255 plus extra for command)
  int count = 0;
  FILE *pp;
  char sockmess[MAXMSG];

  sprintf(cmdline, "ps -C %s -o pid=", procname);
  pp = popen(cmdline, "r");
  while (fscanf(pp, "%d", &procid[count]) != EOF) {	
    count++;
  }
    
  int p;
  int recorded = 0;
  if (count == 0) {
    for (p = 0; p < 128; p++) {
      if (strcmp(procname, alreadyreported[p]) == 0) {
	recorded = 1;
      }
    }
    if (recorded == 0) {
      time_t currtime;
      time(&currtime);
      sprintf(sockmess, "[%.*s] Info: No '%s' process found.\n", (int) strlen(ctime(&currtime))-1, ctime(&currtime), procname);
      write(sock, &sockmess, MAXMSG);
      memcpy(alreadyreported[index], procname, 255);
    }
  } else {
    memcpy(alreadyreported[index], " ", 255);
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
  pni = popen("ps -C procnanny.client -o pid=", "r");
  
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
