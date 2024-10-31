#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "wrappers.h"
#include "message.h"
#include "shmem.h"

typedef struct {
    int totalParts;
    int totalIterations;
} FactoryStats;

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <num_factories>\n", argv[0]);
        exit(1);
    }

    int numFactories = atoi(argv[1]);
    int activeFactories = numFactories;

    // Connect to shared memory
    key_t shmkey = ftok("sales.c", 1);
    int shmid = Shmget(shmkey, SHMEM_SIZE, S_IRUSR | S_IWUSR);
    shData *sharedData = (shData *)Shmat(shmid, NULL, 0);

    // Connect to message queue
    key_t msgkey = ftok("factory.c", 1);
    int msgid = Msgget(msgkey, S_IRUSR | S_IWUSR);

    // Connect to semaphores
    sem_t *sem_rendezvous = sem_open("/cassadjx_rendezvous_sem", 0);
    sem_t *printReportSem = sem_open("/cassadjx_print_report_sem", 0);
    
    if (sem_rendezvous == SEM_FAILED || printReportSem == SEM_FAILED) {
        perror("sem_open");
        exit(1);
    }

    // Array to store statistics for each factory
    FactoryStats *stats = calloc(numFactories, sizeof(FactoryStats));
    if (!stats) {
        perror("calloc");
        exit(1);
    }

    printf("Supervisor: Starting to monitor %d factories\n", numFactories);
    fflush(stdout);

    // Process messages until all factories are done
    msgBuf msg;
    while (activeFactories > 0) {
        // Important: sizeof(msg) - sizeof(long) for the size
        if (msgrcv(msgid, &msg, sizeof(msgBuf) - sizeof(long), 0, 0) == -1) {
            perror("msgrcv");
            exit(1);
        }

        if (msg.purpose == PRODUCTION_MSG) {
            printf("Supervisor: Factory %d produced %d parts in %d milliseconds\n",
                   msg.facID, msg.partsMade, msg.duration);
            fflush(stdout);
            
            // Update statistics
            stats[msg.facID - 1].totalParts += msg.partsMade;
            stats[msg.facID - 1].totalIterations++;
            
        } else if (msg.purpose == COMPLETION_MSG) {
            printf("Supervisor: Factory %d Terminated\n", msg.facID);
            fflush(stdout);
            activeFactories--;
        }
    }

    printf("Supervisor: All factories have completed\n");
    fflush(stdout);

    // Signal sales that manufacturing is complete
    Sem_post(sem_rendezvous);

    // Wait for permission to print final report
    Sem_wait(printReportSem);

    // Print final report
    printf("\nSupervisor: Manufacturing Statistics:\n");
    printf("=====================================\n");
    int grandTotal = 0;
    
    for (int i = 0; i < numFactories; i++) {
        printf("Factory %d: Made %d parts in %d iterations\n",
               i + 1, stats[i].totalParts, stats[i].totalIterations);
        grandTotal += stats[i].totalParts;
    }
    
    printf("\nTotal parts manufactured: %d (Original order: %d)\n",
           grandTotal, sharedData->order_size);
    fflush(stdout);

    // Cleanup
    free(stats);
    sem_close(sem_rendezvous);
    sem_close(printReportSem);
    Shmdt(sharedData);
    
    return 0;
}