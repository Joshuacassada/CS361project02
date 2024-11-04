//---------------------------------------------------------------------
// Assignment : PA-02 Concurrent Processes & IPC
// Date       : 11/04/2024
// Authors    : Joshua Cassada (cassadjx@dukes.jmu.edu) and Thomas Cantrell (cantretw@dukes.jmu.edu)
// File name  : sales.c
//----------------------------------------------------------------------
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

// This is our global variables initializing what we will need.
int msgid = -1;
int shmid = -1;
sem_t *sem_rendezvous = NULL;
sem_t *sem_factory_log = NULL;
sem_t *printReportSem = NULL;
pid_t childPids[MAXFACTORIES + 1];
shData *sharedData = NULL;
int numChildren = 0;

// Cleanup method that closes and unlinks semaphores if necessary that we can call again when needed in the code.
void cleanup() {
    for (int i = 0; i < numChildren; i++) {
        if (childPids[i] > 0) {
            kill(childPids[i], SIGKILL);
        }
    }

    if (sem_rendezvous) {
        Sem_close(sem_rendezvous);
        Sem_unlink("/cantretw_rendezvous_sem");
    }
    if (sem_factory_log) {
        Sem_close(sem_factory_log);
        Sem_unlink("/cantretw_sem_factory_log");
    }
    if (printReportSem) {
        Sem_close(printReportSem);
        Sem_unlink("/cantretw_print_report_sem");
    }
    if (sharedData) {
        Shmdt(sharedData);
    }
    if (shmid >= 0) {
        shmctl(shmid, IPC_RMID, NULL);
    }
    if (msgid >= 0) {
        msgctl(msgid, IPC_RMID, NULL);
    }
}

// This is our goodbye method that we use to control the SIGTERM, SIGINT, and default interruptions.
// And we call cleanup at the end to close any semaphores necessary.
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

//This is our main function that runs the rest of the code needed for sales. 
int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <numfactories> <ordersize>\n", argv[0]);
        exit(1);
    }
    //This is the intializer for number of factories and order size using argv[1] and argv[2]
    int numfactories = atoi(argv[1]);
    int ordersize = atoi(argv[2]);

    sigactionWrapper(SIGTERM, goodbye);
    sigactionWrapper(SIGINT, goodbye);

    if (numfactories > MAXFACTORIES || numfactories < 1) {
        fprintf(stderr, "Number of factories must be between 1 and %d\n", MAXFACTORIES);
        exit(1);
    }

    //This is where we get shared memory and message queue for the sales and factory.
    int shmflg = IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR;
    int semmode = S_IRUSR | S_IWUSR;
    int semflg = O_CREAT | O_EXCL;

    key_t shmkey = ftok("sales.c", 1);
    shmid = Shmget(shmkey, SHMEM_SIZE, shmflg);
    key_t msgkey = ftok("factory.c", 1);
    msgid = Msgget(msgkey, shmflg);
    
    sharedData = (shData *)Shmat(shmid, NULL, 0);
    srandom(time(NULL));

    sharedData->order_size = ordersize;
    sharedData->made = 0;
    sharedData->remain = ordersize;
    sharedData->activeFactories = numfactories;
    sem_rendezvous = Sem_open("/cantretw_rendezvous_sem", semflg, semmode, 0);
    sem_factory_log = Sem_open("/cantretw_sem_factory_log", semflg, semmode, 1);
    printReportSem = Sem_open("/cantretw_print_report_sem", semflg, semmode, 0);

    printf("SALES: Will Request an Order of Size = %d parts\n", ordersize);
    printf("Creating %d Factory(ies)\n", numfactories);


    //This is where you open supervisor.log
    int fc = open("supervisor.log", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fc == -1) {
        perror("error opening supervisor.log");
        cleanup();
        exit(1);
    }

    // This is the fork for supervisor where you need to do the checks for errors
    // and you must duplicate the file.
    pid_t supPid = Fork();
    if (supPid == 0) {
        dup2(fc, fileno(stdout));
        close(fc);
        char numfactories_str[50];
        sprintf(numfactories_str, "%d", numfactories);
        execlp("./supervisor", "supervisor", numfactories_str, NULL);
        perror("execlp supervisor");
        exit(1);
    }
    close(fc);
    childPids[numChildren++] = supPid;

    // This is the factory.log where you have to open.
    int fd = open("factory.log", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd == -1) {
        perror("error opening factory.log");
        cleanup();
        exit(1);
    }

    // This is the fork for factory where you have to get capacity and duration.
    // and you have to exec and check for errors. 
    for (int i = 0; i < numfactories; i++) {
        int capacity = (random() % 41) + 10;
        int duration = (random() % 701) + 500;
        
        pid_t factory = Fork();
        if (factory == 0) {
            dup2(fd, fileno(stdout));
            close(fd);
            char factoryid[10], cap[10], dur[10];
            sprintf(factoryid, "%d", i + 1);
            sprintf(cap, "%d", capacity);
            sprintf(dur, "%d", duration);
            execlp("./factory", "factory", factoryid, cap, dur, NULL);
            perror("execlp factory");
            exit(1);
        }
        childPids[numChildren++] = factory;
        
        printf("SALES: Factory # %3d was created, with Capacity=%4d and Duration=%4d\n",
               i + 1, capacity, duration);
    }

    //Lastly this is where you wait and post depending on when the code is supposed to run from factory
    // or supervisor. And lastly call cleanup to close and unlink the semaphores necessary.
    close(fd);
    Sem_wait(sem_rendezvous);
    printf("SALES: Supervisor says all Factories have completed their mission\n");
    sleep(2);
    printf("SALES: Permission granted to print the final report\n");
    Sem_post(printReportSem);
    
    printf("SALES: Cleaning up after the Supervisor Factory Processes\n");
    for (int i = 0; i < numChildren; i++) {
        wait(NULL);
    }

    cleanup();
    return 0;
}