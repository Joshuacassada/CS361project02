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
    int shmid = shmget(shmkey, SHMEM_SIZE, S_IRUSR | S_IWUSR);
    if (shmid == -1) {
        perror("shmget failed");
        exit(1);
    }
    
    shData *sharedData = shmat(shmid, NULL, 0);
    if (sharedData == (void *)-1) {
        perror("shmat failed");
        exit(1);
    }

    key_t msgkey = ftok("factory.c", 1);
    int msgid = msgget(msgkey, S_IRUSR | S_IWUSR);
    if (msgid == -1) {
        perror("msgget failed");
        exit(1);
    }
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

        usleep(duration * 1000);

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

        if (msgsnd(msgid, &msg, sizeof(msgBuf) - sizeof(long), 0) == -1) {
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

    if (msgsnd(msgid, &msg, sizeof(msgBuf) - sizeof(long), 0) == -1) {
        perror("msgsnd completion failed");
        exit(1);
    }

    Sem_wait(sem_factory_log);
    printf(">>> Factory # %2d: Terminating after making total of %4d parts in %4d iterations\n",
           factoryId, totalPartsMade, iterations);
    fflush(stdout);
    Sem_post(sem_factory_log);

    sem_close(sem_factory_log);
    shmdt(sharedData);
    return 0;
}