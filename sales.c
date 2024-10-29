#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include "wrappers.h"
#include "message.h"
#include "shmem.h"
#include <sys/stat.h>

int msgid = -1;
int shmid = -1;
sem_t *sem_rendezvous;
sem_t *sem_factory_log;
pid_t childPids[MAXFACTORIES];
sem_t *printReportSem;

void cleanup() {
   if (shmid != -1) {
       shmctl(shmid, IPC_RMID, NULL);
   }
   if (msgid != -1) {
       msgctl(msgid, IPC_RMID, NULL);
   }
   
    Sem_close(sem_factory_log);
    Sem_unlink("/cassadjx_sem_factory_log");
    Sem_close(sem_rendezvous);
    Sem_unlink("/cassadjx_rendezvous_sem");
    Sem_close(printReportSem);
    Sem_unlink("/cassadjx_print_report_sem");
}

void goodbye(int sig) {
   fflush(stdout);
   switch(sig) {
       case SIGTERM:
           printf("nicely asked to TERMINATE by SIGTERM (%d).\n", sig);
           break;
       case SIGINT:
           printf("INTERRUPTED by SIGINT (%d)\n", sig);
           break;
       default:
           printf("unexpectedly SIGNALed by (%d)\n", sig);
   }
   cleanup();
   exit(0);
}

int main(int argc, char *argv[]){

   if (argc != 3) {
       fprintf(stderr, "Usage: %s <numfactories> <ordersize>\n", argv[0]);
       exit(1);
   }
   pid_t pid = getpid();

   int numfactories = atoi(argv[1]);
   int ordersize = atoi(argv[2]);

   sigactionWrapper(SIGTERM, goodbye);
   sigactionWrapper(SIGINT, goodbye);

   if (numfactories > MAXFACTORIES || numfactories < 1){
       fprintf(stderr, "Number of factories must be between 1 and the %d\n", MAXFACTORIES);
       exit(1);
   }

   int shmflg = IPC_CREAT | 0666;
   int semmode = S_IRUSR | S_IWUSR;
   int semflg = O_CREAT | O_EXCL;

   // Create shared memory
   key_t shmkey = ftok(".", pid);
   shmid = Shmget(shmkey, SHMEM_SIZE, shmflg);

   // Create message queue
   key_t msgkey = ftok(".", pid + 1);
   msgid = Msgget(msgkey, shmflg);

   shData *sharedData = (shData *)Shmat(shmid, NULL, 0);

   sharedData->order_size = ordersize;
   sharedData->made = 0;
   sharedData->remain = ordersize;
   sharedData->activeFactories = numfactories;

    sem_rendezvous = sem_open("/cassadjx_rendezvous_sem", semflg, semmode, 0);
    
    sem_factory_log = sem_open("/cassadjx_sem_factory_log", semflg, semmode, 1);
    
    printReportSem = sem_open("/cassadjx_print_report_sem", semflg, semmode, 0);


   printf("SALES: Will Request an Order of Size = %d parts\n", ordersize);
   printf("Creating %d Factory(ies)\n", numfactories);

   int fc = open("supervisor.log", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
   if (fc == -1){
       fprintf(stderr, "error opening supervisor.log");
       exit(1);
   }
   pid_t supPid = Fork();
   if (supPid == 0){
       dup2(fc, fileno(stdout));
       char numfactories_str[50];
       sprintf(numfactories_str, "%d", numfactories);
       execlp("./supervisor", "supervisor", numfactories_str, NULL);
       perror("execlp supervisor");
       exit(1);
   }
   

   int fd = open("factory.log", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
   if (fd == -1){
       fprintf(stderr, "error opening factory.log");
       exit(1);
   }


   for (int i = 0; i < numfactories; i++) {
       int capacity = (random() % 41) + 10;
       int duration = (random() % 701) + 500;
       
       pid_t factory = Fork();
       if (factory == 0){
           dup2(fd, fileno(stdout));
           char factoryid[10], cap[10], dur[10];
           sprintf(factoryid, "%d", i + 1);  // Use factory number instead of PID
           sprintf(cap, "%d", capacity);
           sprintf(dur, "%d", duration);
           execlp("./factory", "factory", factoryid, cap, dur, NULL);
           perror("execlp factory");
           exit(1);
       }
       childPids[i] = factory;
   
       printf("SALES: Factory # %3d was created, with Capacity=%4d and Duration=%4d\n",
              i + 1, capacity, duration);
   }

   Sem_wait(sem_rendezvous);

   Usleep(2000);

   printf("SALES: Permission granted to print the final report\n");

   Sem_post(printReportSem);
   
   printf("SALES: Cleaning up after the Supervisor Factory Processes\n");

   for (int i = 0; i < numfactories; i++) {
       waitpid(childPids[i], NULL, 0);
   }

   cleanup();
   
   return 0;
}