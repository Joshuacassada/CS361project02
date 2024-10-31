// factory.c
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
        fprintf(stderr, "Factory: Usage: %s <factory_id> <capacity> <duration>\n", argv[0]);
        exit(1);
    }

    int factoryId = atoi(argv[1]);
    int capacity = atoi(argv[2]);
    int duration = atoi(argv[3]);

    // Connect to shared memory
    key_t shmkey = ftok("sales.c", 1);
    if (shmkey == -1) {
        perror("Factory: ftok failed for shared memory");
        exit(1);
    }
    
    int shmid = shmget(shmkey, SHMEM_SIZE, 0666);
    if (shmid == -1) {
        perror("Factory: shmget failed");
        exit(1);
    }
    
    shData *sharedData = shmat(shmid, NULL, 0);
    if (sharedData == (void *)-1) {
        perror("Factory: shmat failed");
        exit(1);
    }

    // Connect to message queue
    key_t msgkey = ftok("factory.c", 1);
    if (msgkey == -1) {
        perror("Factory: ftok failed for message queue");
        exit(1);
    }
    
    int msgid = msgget(msgkey, 0666);
    if (msgid == -1) {
        perror("Factory: msgget failed");
        exit(1);
    }

    // Connect to semaphore
    sem_t *sem_factory_log = Sem_open2("/cassadjx_sem_factory_log", 0);

    printf("Factory %d: Successfully connected to IPC mechanisms\n", factoryId);
    fflush(stdout);

    int iterations = 0;
    int totalPartsMade = 0;

    while (1) {
        Sem_wait(sem_factory_log);
        if (sharedData->remain <= 0) {
            Sem_post(sem_factory_log);
            break;
        }
        
        int partsToMake = (sharedData->remain < capacity) ? sharedData->remain : capacity;
        sharedData->remain -= partsToMake;
        
        printf("Factory %d: Making %d parts in %d milliseconds\n", 
               factoryId, partsToMake, duration);
        fflush(stdout);
        Sem_post(sem_factory_log);

        Usleep(duration * 1000);

        Sem_wait(sem_factory_log);
        sharedData->made += partsToMake;
        totalPartsMade += partsToMake;
        iterations++;
        
        // Send production message
        msgBuf msg = {0};  // Initialize to zero
        msg.mtype = 1;
        msg.purpose = PRODUCTION_MSG;
        msg.facID = factoryId;
        msg.capacity = capacity;
        msg.partsMade = partsToMake;
        msg.duration = duration;

        if (msgsnd(msgid, &msg, sizeof(msgBuf) - sizeof(long), 0) == -1) {
            perror("Factory: msgsnd production failed");
            Sem_post(sem_factory_log);
            exit(1);
        }
        
        printf("Factory %d: Sent production message for %d parts\n", 
               factoryId, partsToMake);
        fflush(stdout);
        Sem_post(sem_factory_log);
    }

    // Send completion message
    msgBuf msg = {0};  // Initialize to zero
    msg.mtype = 1;
    msg.purpose = COMPLETION_MSG;
    msg.facID = factoryId;
    msg.capacity = capacity;
    msg.partsMade = totalPartsMade;
    msg.duration = duration;

    if (msgsnd(msgid, &msg, sizeof(msgBuf) - sizeof(long), 0) == -1) {
        perror("Factory: msgsnd completion failed");
        exit(1);
    }

    printf("Factory %d: Sent completion message. Total parts made: %d\n", 
           factoryId, totalPartsMade);
    fflush(stdout);

    sem_close(sem_factory_log);
    shmdt(sharedData);
    return 0;
}