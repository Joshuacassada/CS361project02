#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "wrappers.h"
#include "message.h"
#include "shmem.h"

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <num_factories>\n", argv[0]);
        exit(1);
    }

    int numFactories = atoi(argv[1]);
    int factoryParts[MAXFACTORIES] = {0};
    int factoryIterations[MAXFACTORIES] = {0};

    key_t shmkey = ftok("sales.c", 1);
    int shmid = Shmget(shmkey, SHMEM_SIZE, S_IRUSR | S_IWUSR);
    shData *sharedData = Shmat(shmid, NULL, 0);

    key_t msgkey = ftok("factory.c", 1);
    int msgid = Msgget(msgkey, S_IRUSR | S_IWUSR);

    sem_t *sem_rendezvous = Sem_open2("/cantretw_rendezvous_sem", 0);
    sem_t *printReportSem = Sem_open2("/cantretw_print_report_sem", 0);
    printf("SUPERVISOR: Started\n");
    fflush(stdout);

    int activeFactories = numFactories;
    msgBuf msg;

    while (activeFactories > 0) {
        if (msgrcv(msgid, &msg, MSG_INFO_SIZE, 1, 0) == -1) {
            perror("msgrcv failed");
            exit(1);
        }
        int facIndex = msg.facID - 1;
        
        if (msg.purpose == PRODUCTION_MSG) {
            printf("SUPERVISOR: Factory # %d produced %3d parts in %4d milliSecs\n",
                   msg.facID, msg.partsMade, msg.duration);
            fflush(stdout);
            
            factoryParts[facIndex] += msg.partsMade;
            factoryIterations[facIndex]++;
        }
        else if (msg.purpose == COMPLETION_MSG) {
            printf("SUPERVISOR: Factory # %d        COMPLETED its task\n", msg.facID);
            fflush(stdout);
            activeFactories--;
        }
    }
    printf("SUPERVISOR: Manufacturing is complete. Awaiting permission to print final report\n");
    fflush(stdout);

    Sem_post(sem_rendezvous);
    Sem_wait(printReportSem);

    printf("\n****** SUPERVISOR: Final Report ******\n");
    int grandTotal = 0;
    for (int i = 0; i < numFactories; i++) {
        printf("Factory # %d made a total of %4d parts in %5d iterations\n",
               i + 1, factoryParts[i], factoryIterations[i]);
        grandTotal += factoryParts[i];
    }
    printf("===============================\n");
    printf("Grand total parts made = %d    vs    order size of %d\n\n",
           grandTotal, sharedData->order_size);
    printf(">>> Supervisor Terminated\n");
    fflush(stdout);
    Sem_close(sem_rendezvous);
    Sem_close(printReportSem);
    Shmdt(sharedData);

    return 0;
}