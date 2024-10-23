#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <signal.h>
#include <semaphore.h>
#include <fcntl.h>
#include <time.h>
#include "wrappers.h"
#include "shmem.h"

// Global variables for cleanup
int shm_id;
int msg_queue_id;
sem_t *factory_log_sem;  // For factory.log mutual exclusion
sem_t *finished_sem;     // For supervisor to signal completion
sem_t *print_report_sem; // For allowing supervisor to print report
shData *shared_data;     // Pointer to shared memory
pid_t *child_pids;      // Array to store child PIDs
int num_factories;

// Cleanup function
void cleanup() {
    // Detach from shared memory
    if (shared_data != NULL)
        Shmdt(shared_data);
    
    // Remove shared memory
    if (shm_id >= 0)
        shmctl(shm_id, IPC_RMID, NULL);
    
    // Remove message queue
    if (msg_queue_id >= 0)
        msgctl(msg_queue_id, IPC_RMID, NULL);
    
    // Close and unlink semaphores
    if (factory_log_sem != NULL) {
        Sem_close(factory_log_sem);
        Sem_unlink("/factory_log_sem");
    }
    if (finished_sem != NULL) {
        Sem_close(finished_sem);
        Sem_unlink("/finished_sem");
    }
    if (print_report_sem != NULL) {
        Sem_close(print_report_sem);
        Sem_unlink("/print_report_sem");
    }
}

void signal_handler(int signum) {
    printf("\nReceived signal %d. Cleaning up...\n", signum);
    
    // Kill all child processes
    for (int i = 0; i < num_factories + 1; i++) {
        if (child_pids[i] > 0) {
            kill(child_pids[i], SIGTERM);
        }
    }
    
    cleanup();
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    num_factories = atoi(argv[1]);
    int arg2 = atoi(argv[2]);

    if (num_factories <= 0 || num_factories > MAXFACTORIES) {
        fprintf(stderr, "Number of factories must be between 1 and %d\n", MAXFACTORIES);
        exit(EXIT_FAILURE);
    }
    if (arg2 <= 0) {
        fprintf(stderr, "Order size must be positive\n");
        exit(EXIT_FAILURE);
    }

    sigactionWrapper(SIGINT, signal_handler);
    sigactionWrapper(SIGTERM, signal_handler);

    pid_t child_pids[MAXFACTORIES + 1];
    
    // Create keys for shared memory and message queue
    key_t shm_key = ftok(".", 's');
    key_t msg_key = ftok(".", 'm');
    
    // Create and attach to shared memory
    shm_id = Shmget(shm_key, SHMEM_SIZE, IPC_CREAT | 0666);
    shared_data = (shData *)Shmat(shm_id, NULL, 0);

    

}

