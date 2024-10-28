#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "wrappers.h"
#include "message.h"
#include "shmem.h"

typedef struct {
    int totalParts;
    int iterations;
} FactoryStats;

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <num_factories>\n", argv[0]);
        exit(1);
    }

    // Get semaphore name from environment
    const char *factoryLogSem = getenv("FACTORY_LOG_SEM");
    const char *supDoneSem = getenv("SUPERVISOR_DONE_SEM");
    const char *printReportSem = getenv("PRINT_REPORT_SEM");
    
    if (!factoryLogSem || !supDoneSem || !printReportSem) {
        fprintf(stderr, "Required environment variables not set\n");
        exit(1);
    }
    
    int numFactories = atoi(argv[1]);
    FactoryStats *stats = calloc(numFactories, sizeof(FactoryStats));
    
    // Reattach to shared memory
    key_t shmkey = ftok(".", getppid());
    int shmid = Shmget(shmkey, SHMEM_SIZE, 0666);
    shData *sharedData = (shData *)Shmat(shmid, NULL, 0);
    
    // Reattach to message queue
    key_t msgkey = ftok(".", getppid() + 1);
    int msgid = Msgget(msgkey, 0666);
    
    // Open existing semaphores
    sem_t *supDoneSemaphore = Sem_open2(supDoneSem, 0);
    sem_t *printReportSemaphore = Sem_open2(printReportSem, 0);
    
    printf("SUPERVISOR: Started\n");
    
    // Process messages until all factories are done
    int activeFactories = numFactories;
    msgBuf msg;
    
    while (activeFactories > 0) {
        if (msgrcv(msgid, &msg, MSG_INFO_SIZE, 0, 0) == -1) {
            perror("msgrcv failed");
            exit(1);
        }
        
        int facIdx = msg.facID - 1;
        
        if (msg.purpose == PRODUCTION_MSG) {
            printf("SUPERVISOR: Factory # %d produced %d parts in %d milliSecs\n",
                   msg.facID, msg.partsMade, msg.duration);
            
            stats[facIdx].totalParts += msg.partsMade;
            stats[facIdx].iterations++;
        }
        else if (msg.purpose == COMPLETION_MSG) {
            printf("SUPERVISOR: Factory # %d COMPLETED its task\n", msg.facID);
            activeFactories--;
        }
    }
    
    printf("SUPERVISOR: Manufacturing is complete. Awaiting permission to print final report\n");
    
    // Signal sales that manufacturing is complete
    Sem_post(supDoneSemaphore);
    
    // Wait for permission to print report
    Sem_wait(printReportSemaphore);
    
    // Print final report
    printf("\n****** SUPERVISOR: Final Report ******\n");
    int grandTotal = 0;
    
    for (int i = 0; i < numFactories; i++) {
        printf("Factory # %d made a total of %d parts in %d iterations\n",
               i + 1, stats[i].totalParts, stats[i].iterations);
        grandTotal += stats[i].totalParts;
    }
    
    printf("=====================================\n");
    printf("Grand total parts made = %d vs order size of %d\n",
           grandTotal, sharedData->order_size);
    
    // Clean up
    free(stats);
    Shmdt(sharedData);
    Sem_close(supDoneSemaphore);
    Sem_close(printReportSemaphore);
    
    printf(">>> Supervisor Terminated\n");
    return 0;
}