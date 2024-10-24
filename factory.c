#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "wrappers.h"
#include "shmem.h"
#include "message.h"

void sendMessageToSupervisor(int msgid, int factoryId, int capacity, 
                           int partsMade, int duration, msgPurpose_t purpose) {
    msgBuf msg;
    msg.mtype = 1;  // Any non-zero value
    msg.purpose = purpose;
    msg.facID = factoryId;
    msg.capacity = capacity;
    msg.partsMade = partsMade;
    msg.duration = duration;

    if (msgsnd(msgid, &msg, MSG_INFO_SIZE, 0) == -1) {
        perror("Failed to send message to supervisor");
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <factory_id> <capacity> <duration>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Parse command line arguments
    int factoryId = atoi(argv[1]);
    int capacity = atoi(argv[2]);
    int duration = atoi(argv[3]);

    if (factoryId <= 0 || factoryId > MAXFACTORIES) {
        fprintf(stderr, "Invalid factory ID\n");
        exit(EXIT_FAILURE);
    }

    if (capacity < 10 || capacity > 50) {
        fprintf(stderr, "Capacity must be between 10 and 50\n");
        exit(EXIT_FAILURE);
    }

    if (duration < 500 || duration > 1200) {
        fprintf(stderr, "Duration must be between 500 and 1200 milliseconds\n");
        exit(EXIT_FAILURE);
    }

    // Connect to shared memory
    key_t shmkey = ftok(".", 'S');
    int shmid = Shmget(shmkey, SHMEM_SIZE, 0666);
    shData *sharedData = (shData *)Shmat(shmid, NULL, 0);

    // Connect to message queue
    key_t msgkey = ftok(".", 'M');
    int msgid = Msgget(msgkey, 0666);

    // Connect to factory log semaphore for synchronized output
    sem_t *factoryLogSem = Sem_open2("/factory_log_sem", 0);

    int iterations = 0;
    int totalPartsMade = 0;

    // Main manufacturing loop
    while (1) {
        // Critical section: check remaining parts and update if needed
        int partsToMake;
        
        // Get mutex for shared memory access
        Sem_wait(factoryLogSem);
        
        if (sharedData->remain <= 0) {
            // No more parts to make
            Sem_post(factoryLogSem);
            break;
        }

        // Determine how many parts to make this iteration
        partsToMake = (sharedData->remain < capacity) ? sharedData->remain : capacity;
        sharedData->remain -= partsToMake;
        
        // Print status before manufacturing
        printf("Factory #%3d: Going to make %5d parts in %4d milliSecs\n",
               factoryId, partsToMake, duration);
        fflush(stdout);
        
        Sem_post(factoryLogSem);

        // Simulate manufacturing time
        Usleep(duration * 1000);  // Convert milliseconds to microseconds

        // Update statistics and send production message
        totalPartsMade += partsToMake;
        iterations++;

        // Critical section: update made count
        Sem_wait(factoryLogSem);
        sharedData->made += partsToMake;
        
        // Print completion of this batch
        printf("Factory #%3d: Completed making %5d parts\n",
               factoryId, partsToMake);
        fflush(stdout);
        
        Sem_post(factoryLogSem);

        // Send production message to supervisor
        sendMessageToSupervisor(msgid, factoryId, capacity, 
                              partsToMake, duration, PRODUCTION_MSG);
    }

    // Critical section: print final status
    Sem_wait(factoryLogSem);
    printf("Factory Line %d: Completed after making total of %d parts in %d iterations\n",
           factoryId, totalPartsMade, iterations);
    fflush(stdout);
    Sem_post(factoryLogSem);

    // Send completion message to supervisor
    sendMessageToSupervisor(msgid, factoryId, capacity, 
                          totalPartsMade, duration, COMPLETION_MSG);

    // Clean up
    Sem_close(factoryLogSem);
    Shmdt(sharedData);

    return 0;
}