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

sem_t *sem_rendezvous;
sem_t *sem_factory_log;
pid_t *childPids = NULL;


void goodbye( int sig ) {
    fflush( stdout ) ;
    printf( "\n### LEFT has been " );
    switch( sig )
        {
        case SIGTERM:
            printf("nicely asked to TERMINATE by SIGTERM ( %d ).\n" , sig );
            break ;
        case SIGINT:
            printf("INTERRUPTED by SIGINT ( %d )\n" , sig );
            break ;
        default:
            printf("unexpectedly SIGNALed by ( %d )\n" , sig );
        }

}
int main(int argc, char *argv[]){

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <numfactories> <ordersize>\n", argv[0]);
        exit(1);
    }
    pid_t pid = getpid();

    int numfactories = atoi(argv[1]);
    int ordersize = atoi(argv[2]);

    sigactionWrapper( SIGTERM ,  goodbye) ;
    sigactionWrapper( SIGINT ,  goodbye) ;

    if (numfactories > MAXFACTORIES || numfactories < 1){
        fprintf(stderr, "Number of factories must be between 1 and the %d\n", MAXFACTORIES);
        exit(1);
    }

    int shmflg = IPC_CREAT | IPC_EXCL;
    int semmode = S_IRUSR | S_IWUSR;
    int semflg = O_CREAT | O_EXCL;
    key_t shmkey = ftok("shmem.h", pid);
    int shmid = Shmget(shmkey, SHMEM_SIZE, shmflg);

    shData *sharedData = (shData *)Shmat(shmid, NULL, 0);

    sharedData->order_size = ordersize;
    sharedData->made = 0;
    sharedData->remain = ordersize;
    sharedData->activeFactories = numfactories;

    sem_rendezvous = Sem_open("/rendezvous_sem", semflg, semmode, 0);
    sem_factory_log = Sem_open("/sem_factory_log", semflg, semmode, 1);

    print("SALES: Will Request an Order of Size = %d parts\n", ordersize);

    printf("Creating %d Factory(ies)\n", numfactories);


    pid_t supPid = Fork();
    if (supPid == 0){
        dup2("supervisor.log", stdout);

        char numfactories[10];
        sprintf("Number of factories: %d", numfactories);
        execlp("./supervisor", "supervisor", NULL);
        perror("execlp supervisor");
        exit(1);
    }

    for (int i = 0; i < numfactories; i++) {
        msgBuf * capacity = (msgBuf *) capacity;
        msgBuf * duration = (msgBuf *) duration;
        pid_t factory = Fork();
        if (factory == 0){

            dup2("factory.log", stdout);
            char factoryid[10], cap[10], dur[10];
            sprintf(factoryid, "%d", factoryid);
            sprintf(cap, "%d", capacity);
            sprintf(dur, "%d", duration);
            execlp("./factory", "factory", NULL);
            perror("execlp factory");
            exit(1);
        }
    childPids[i + 1] = factory;
    
    printf("SALES: Factory # %3d was created, with Capacity=%4d and Duration=%4d\n",
               i + 1, capacity, duration);
    }

    wait(supPid);

    sleep(2);
}