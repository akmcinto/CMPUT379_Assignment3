#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "memwatch.h"

/*********************** Server ***********************/

void killprevprocnanny( void );
int readconfigfile(char *cmdarg);
int getpids(char procname[128][255], int index, FILE *LOGFILE);
void handlesighup(int signum);
void handlesigint(int signum);

u_short PORT = 5000;

// Global variable declarations
char procname[128][255]; // for saving read from file
int numsecs[128];

pid_t procid[128]; // pids corresonding to each proces in config file
  
// Signal flags
int hupflag = 1;
int hupmess = 0;
int inthandle = 0;

int main(int argc, char *argv[])
{
  mwInit();

  // kill any other procnannies
  killprevprocnanny();

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

  signal(SIGHUP, handlesighup);
  signal(SIGINT, handlesigint);

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

