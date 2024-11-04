//---------------------------------------------------------------------
// Assignment : PA-02 Concurrent Processes & IPC
// Date       : 11/04/2024
// Authors    : Joshua Cassada (cassadjx@dukes.jmu.edu) and Thomas Cantrell (cantretw@dukes.jmu.edu)
// File name  : factory.c
//----------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "wrappers.h"
#include "message.h"
#include "shmem.h"

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <factory_id> <capacity> <duration>\n", argv[0]);
        exit(1);
    }
    int factoryId = atoi(argv[1]);
    int capacity = atoi(argv[2]);
    int duration = atoi(argv[3]);

    
    key_t shmkey = ftok("sales.c", 1);
    int shmid = Shmget(shmkey, SHMEM_SIZE, S_IRUSR | S_IWUSR);
    
    shData *sharedData = Shmat(shmid, NULL, 0);
    key_t msgkey = ftok("factory.c", 1);
    int msgid = Msgget(msgkey, S_IRUSR | S_IWUSR);
    sem_t *sem_factory_log = Sem_open2("/cantretw_sem_factory_log", 0);

    Sem_wait(sem_factory_log);
    printf("Factory # %2d: STARTED. My Capacity = %3d, in %4d milliSeconds\n", factoryId, capacity, duration);
    fflush(stdout);
    Sem_post(sem_factory_log);
    int iterations = 0;
    int totalPartsMade = 0;

    while (1) {
        int partsToMake;
        Sem_wait(sem_factory_log);
        if (sharedData->remain <= 0) {
            Sem_post(sem_factory_log);
            break;
        }
        partsToMake = (sharedData->remain < capacity) ? sharedData->remain : capacity;
        sharedData->remain -= partsToMake;
        
        printf("Factory # %2d: Going to make %3d parts in %4d milliSecs\n", 
               factoryId, partsToMake, duration);
        fflush(stdout);
        Sem_post(sem_factory_log);

        Usleep(duration * 1000);

        Sem_wait(sem_factory_log);
        sharedData->made += partsToMake;
        Sem_post(sem_factory_log);

        totalPartsMade += partsToMake;
        iterations++;
        msgBuf msg = {0};
        msg.mtype = 1;
        msg.purpose = PRODUCTION_MSG;
        msg.facID = factoryId;
        msg.capacity = capacity;
        msg.partsMade = partsToMake;
        msg.duration = duration;

        if (msgsnd(msgid, &msg, MSG_INFO_SIZE, 0) == -1) {
            perror("msgsnd production failed");
            exit(1);
        }
    }
    msgBuf msg = {0};
    msg.mtype = 1;
    msg.purpose = COMPLETION_MSG;
    msg.facID = factoryId;
    msg.capacity = capacity;
    msg.partsMade = totalPartsMade;
    msg.duration = duration;

    if (msgsnd(msgid, &msg, MSG_INFO_SIZE, 0) == -1) {
        perror("msgsnd completion failed");
        exit(1);
    }

    Sem_wait(sem_factory_log);
    printf(">>> Factory # %2d: Terminating after making total of %4d parts in %4d iterations\n",
           factoryId, totalPartsMade, iterations);
    fflush(stdout);
    Sem_post(sem_factory_log);

    Sem_close(sem_factory_log);
    Shmdt(sharedData);
    return 0;
}