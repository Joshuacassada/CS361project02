#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <time.h>
#include <semaphore.h>
#include "wrappers.h"
#include "message.h"
#include "shmem.h"

int msgid = -1;
int shmid = -1;
sem_t *sem_manufacturing_done;
sem_t *sem_print_permission;

void cleanup() {
    if (msgid != -1) {
        msgctl(msgid, IPC_RMID, NULL);
    }
    if (shmid != -1) {
        shmctl(shmid, IPC_RMID, NULL);
    }
    if (sem_manufacturing_done != NULL) {
        sem_close(sem_manufacturing_done);
    }
    if (sem_print_permission != NULL) {
        sem_close(sem_print_permission);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <num_factories>\n", argv[0]);
        exit(1);
    }

    int num_factories = atoi(argv[1]);
    if (num_factories <= 0 || num_factories > MAXFACTORIES) {
        fprintf(stderr, "Invalid number of factories. Must be between 1 and %d.\n", MAXFACTORIES);
        exit(1);
    }

    pid_t pid = getpid();
    key_t shmkey = ftok(".", pid);
    key_t msgkey = ftok(".", pid + 1);

    // Attach to shared memory and message queue
    shmid = Shmget(shmkey, SHMEM_SIZE, 0666);
    shData *sharedData = (shData *)Shmat(shmid, NULL, 0);
    msgid = Msgget(msgkey, 0666);

    // Open semaphores
    sem_manufacturing_done = Sem_open("/manufacturing_done", O_CREAT, 0666, 0);
    sem_print_permission = Sem_open("/print_permission", O_CREAT, 0666, 0);

    int active_factories = num_factories;
    int total_parts = 0;

    // Main supervisor loop
    while (active_factories > 0) {
        msgBuf msg;
        Msgrcv(msgid, &msg, sizeof(msg) - sizeof(long), 0, 0);

        if (msg.mtype == PRODUCTION_MSG) {
            total_parts += msg.partsMade;
            printf("Supervisor: Factory %d produced %d parts in %d milliseconds\n",
                   msg.facID, msg.partsMade, msg.duration);
        }
        else if (msg.mtype == COMPLETION_MSG) {
            active_factories--;
            printf("Supervisor: Factory %d has terminated\n", msg.facID);
        }
    }

    // Manufacturing complete, signal Sales
    Sem_post(sem_manufacturing_done);

    // Wait for permission to print report
    Sem_wait(sem_print_permission);

    // Print final report
    printf("\nFinal Manufacturing Report:\n");
    printf("-------------------------\n");
    printf("Total parts manufactured: %d\n", total_parts);
    printf("Order size: %d\n", sharedData->order_size);
    printf("-------------------------\n");

    cleanup();
    return 0;
}