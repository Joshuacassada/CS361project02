#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "wrappers.h"
#include "shmem.h"
#include "message.h"

// Structure to track per-factory statistics
typedef struct {
    int factoryID;
    int totalParts;
    int totalIterations;
    int isActive;
} FactoryStats;

// Compare function for qsort to sort factories by ID
int compareFactories(const void *a, const void *b) {
    return ((FactoryStats *)a)->factoryID - ((FactoryStats *)b)->factoryID;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <num_factories>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int numFactories = atoi(argv[1]);
    if (numFactories <= 0 || numFactories > MAXFACTORIES) {
        fprintf(stderr, "Invalid number of factories\n");
        exit(EXIT_FAILURE);
    }

    // Connect to shared memory
    key_t shmkey = ftok(".", 'S');
    int shmid = Shmget(shmkey, SHMEM_SIZE, 0666);
    shData *sharedData = (shData *)Shmat(shmid, NULL, 0);

    // Connect to message queue
    key_t msgkey = ftok(".", 'M');
    int msgid = Msgget(msgkey, 0666);

    // Connect to semaphores
    sem_t *supervisorReadySem = Sem_open2("/supervisor_ready_sem", 0);
    sem_t *printReportSem = Sem_open2("/print_report_sem", 0);

    // Initialize factory statistics
    FactoryStats *stats = calloc(numFactories, sizeof(FactoryStats));
    for (int i = 0; i < numFactories; i++) {
        stats[i].factoryID = i + 1;
        stats[i].isActive = 1;
    }

    // Main message processing loop
    msgBuf msg;
    int activeFactories = numFactories;
    int grandTotalParts = 0;

    printf("Supervisor started. Monitoring %d factories.\n", numFactories);

    while (activeFactories > 0) {
        // Receive message from any factory
        int msgSize = msgrcv(msgid, &msg, MSG_INFO_SIZE, 0, 0);
        if (msgSize == -1) {
            perror("Message receive failed");
            continue;
        }

        int factoryIndex = msg.facID - 1;
        
        switch (msg.purpose) {
            case PRODUCTION_MSG:
                printf("Factory %d produced %d parts in %d milliseconds\n",
                       msg.facID, msg.partsMade, msg.duration);

                // Update factory statistics
                stats[factoryIndex].totalParts += msg.partsMade;
                stats[factoryIndex].totalIterations++;
                grandTotalParts += msg.partsMade;
                break;

            case COMPLETION_MSG:
                printf("Factory %d has terminated\n", msg.facID);
                stats[factoryIndex].isActive = 0;
                activeFactories--;
                break;

            default:
                printf("Received unsupported message type\n");
                break;
        }
    }

    // Signal sales that manufacturing is complete
    Sem_post(supervisorReadySem);

    // Wait for permission to print final report
    Sem_wait(printReportSem);

    // Sort factories by ID for final report
    qsort(stats, numFactories, sizeof(FactoryStats), compareFactories);

    // Print final manufacturing report
    printf("\n=== Final Manufacturing Report ===\n");
    printf("%-12s %-15s %-15s\n", "Factory ID", "Total Parts", "Total Iterations");
    printf("----------------------------------------\n");

    for (int i = 0; i < numFactories; i++) {
        printf("%-12d %-15d %-15d\n",
               stats[i].factoryID,
               stats[i].totalParts,
               stats[i].totalIterations);
    }

    printf("----------------------------------------\n");
    printf("Grand Total Parts Manufactured: %d\n", grandTotalParts);
    printf("Order Size: %d\n", sharedData->order_size);
    
    if (grandTotalParts == sharedData->order_size) {
        printf("✓ Production completed successfully - totals match\n");
    } else {
        printf("⚠ Warning: Production total doesn't match order size!\n");
    }

    // Clean up
    free(stats);
    Sem_close(supervisorReadySem);
    Sem_close(printReportSem);
    Shmdt(sharedData);

    return 0;
}