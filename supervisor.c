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
sem_t *sem_rendezvous;

void cleanup() {
    if (msgid != -1) {
        msgctl(msgid, IPC_RMID, NULL);
    }
    if (shmid != -1) {
        shmctl(shmid, IPC_RMID, NULL);
    }
    if (sem_rendezvous != NULL) {
        sem_close(sem_rendezvous);
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

    int semmode = S_IRUSR | S_IWUSR;
    int semflg = O_CREAT | O_EXCL;


    pid_t pid = getpid();
    key_t shmkey = ftok(".", pid);    // Same key as used in sales.c for shared memory
    key_t msgkey = ftok(".", pid + 1); // Same key as used in sales.c for message queue

    // Attach to shared memory
    shmid = Shmget(shmkey, SHMEM_SIZE, 0666); // Connect to the existing shared memory
    shData *sharedData = (shData *)Shmat(shmid, NULL, 0);

    // Attach to the message queue
    msgid = Msgget(msgkey, 0666); // Connect to the existing message queue

    // Open the semaphore for synchronization with Sales
    sem_rendezvous = Sem_open("/cassadjx_rendezvous_sem", semflg, semmode, 0);
    if (sem_rendezvous == SEM_FAILED) {
        perror("sem_open /cassadjx_rendezvous_sem failed");
        exit(1);
    }

    int iterations = 0;
    int total_parts_made = 0;

    // Supervisor loop: wait for messages from factories
    while (sharedData->remain > 0) {
        msgBuf msg;
        int result = Msgrcv(msgid, &msg, sizeof(msg) - sizeof(long), 0, 0);
        if (result == -1) {
            perror("Error receiving message");
            cleanup();
            exit(1);
        }

        int parts_to_make = (sharedData->remain < msg.capacity) ? sharedData->remain : msg.capacity;
        sharedData->remain -= parts_to_make;
        total_parts_made += parts_to_make;

        printf("Factory #%d: Going to make %d parts in %d milliseconds\n", msg.facID, parts_to_make, msg.duration);
        usleep(msg.duration * 1000); // Sleep for the specified duration (in milliseconds)
        iterations++;

        // Send production message
        msgBuf production_msg;
        production_msg.mtype = msg.facID;
        production_msg.facID = msg.facID;
        production_msg.partsMade = parts_to_make;
        Msgsnd(msgid, &production_msg, sizeof(production_msg) - sizeof(long), 0);

        printf("Factory #%d has made %d parts\n", msg.facID, parts_to_make);
    }

    // All parts made, send completion message
    for (int i = 1; i <= num_factories; i++) {
        msgBuf completion_msg;
        completion_msg.mtype = i;
        completion_msg.facID = i;
        completion_msg.partsMade = total_parts_made;
        Msgsnd(msgid, &completion_msg, sizeof(completion_msg) - sizeof(long), 0);
    }

    printf("Supervisor: Completed. Total parts made: %d in %d iterations\n", total_parts_made, iterations);

    // Signal Sales process to print final report
    Sem_post(sem_rendezvous);

    cleanup();
    return 0;
}
