#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "wrappers.h"
#include "message.h"
#include "shmem.h"

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <factory_id> <capacity> <duration>\n", argv[0]);
        exit(1);
    }
    
    // Get semaphore name from environment
    const char *factoryLogSem = getenv("FACTORY_LOG_SEM");
    if (!factoryLogSem) {
        fprintf(stderr, "FACTORY_LOG_SEM environment variable not set\n");
        exit(1);
    }
    
    int myID = atoi(argv[1]);
    int capacity = atoi(argv[2]);
    int duration = atoi(argv[3]);
    
    // Reattach to shared memory
    key_t shmkey = ftok(".", getppid());
    int shmid = Shmget(shmkey, SHMEM_SIZE, 0666);
    shData *sharedData = (shData *)Shmat(shmid, NULL, 0);
    
    // Reattach to message queue
    key_t msgkey = ftok(".", getppid() + 1);
    int msgid = Msgget(msgkey, 0666);
    
    // Open existing semaphore
    sem_t *factoryLogSemaphore = Sem_open2(factoryLogSem, 0);
    
    // Print startup message
    Sem_wait(factoryLogSemaphore);
    printf("Factory # %d: STARTED. My Capacity = %d, in %d milliSeconds\n",
           myID, capacity, duration);
    fflush(stdout);
    Sem_post(factoryLogSemaphore);
    
    int iterations = 0;
    int totalPartsMade = 0;
    
    while (1) {
        // Check if there are parts remaining to be made
        int partsToMake = 0;
        
        // Critical section for checking/updating shared memory
        if (sharedData->remain > 0) {
            partsToMake = (sharedData->remain < capacity) ? sharedData->remain : capacity;
            sharedData->remain -= partsToMake;
        }
        
        if (partsToMake == 0) {
            break;  // No more parts to make
        }
        
        // Print production message
        Sem_wait(factoryLogSemaphore);
        printf("Factory # %d: Going to make %d parts in %d milliSecs\n",
               myID, partsToMake, duration);
        fflush(stdout);
        Sem_post(factoryLogSemaphore);
        
        // Simulate production time
        Usleep(duration * 1000);  // Convert to microseconds
        
        // Update statistics
        sharedData->made += partsToMake;
        totalPartsMade += partsToMake;
        iterations++;
        
        // Send production message to supervisor
        msgBuf msg;
        msg.mtype = 1;
        msg.purpose = PRODUCTION_MSG;
        msg.facID = myID;
        msg.capacity = capacity;
        msg.partsMade = partsToMake;
        msg.duration = duration;
        
        if (msgsnd(msgid, &msg, MSG_INFO_SIZE, 0) == -1) {
            perror("msgsnd failed");
            exit(1);
        }
    }
    
    // Send completion message
    msgBuf msg;
    msg.mtype = 1;
    msg.purpose = COMPLETION_MSG;
    msg.facID = myID;
    
    if (msgsnd(msgid, &msg, MSG_INFO_SIZE, 0) == -1) {
        perror("msgsnd failed");
        exit(1);
    }
    
    // Print completion message
    Sem_wait(factoryLogSemaphore);
    printf(">>> Factory # %d: Terminating after making total of %d parts in %d iterations\n",
           myID, totalPartsMade, iterations);
    fflush(stdout);
    Sem_post(factoryLogSemaphore);
    
    // Clean up
    Shmdt(sharedData);
    Sem_close(factoryLogSemaphore);
    
    return 0;
}