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

int getRandom(int min, int max) {
    return min + (random() % (max - min + 1));
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

    shared_data->order_size = arg2;
    shared_data->made = 0;
    shared_data->remain = arg2;
    shared_data->activeFactories = num_factories;

    msg_queue_id = Msgget(msg_key, IPC_CREAT | 0666);

    factory_log_sem = Sem_open("/factory_log_sem", O_CREAT | O_EXCL, 0666, 1);
    finished_sem = Sem_open("/finished_sem", O_CREAT | O_EXCL, 0666, 0);
    print_report_sem = Sem_open("/print_report_sem", O_CREAT | O_EXCL, 0666, 0);
    
    // Create Supervisor process
    pid_t supervisor_pid = Fork();
    if (supervisor_pid == 0) {
        // Child process (Supervisor)
        char num_factories_str[10];
        sprintf(num_factories_str, "%d", num_factories);
        
        // Redirect stdout to supervisor.log
        freopen("supervisor.log", "w", stdout);
        
        execl("./supervisor", "supervisor", num_factories_str, NULL);
        perror("execl supervisor failed");
        exit(EXIT_FAILURE);
    }
    child_pids[0] = supervisor_pid;

    for (int i = 1; i <= num_factories; i++) {
        pid_t factoryPid = Fork();
        if (factoryPid == 0) {
            // Redirect stdout to factory.log
            freopen("factory.log", "w", stdout);
            
            // Generate random capacity and duration for this factory
            int capacity = getRandom(10, 50);
            int duration = getRandom(500, 1200);
            
            // Convert numbers to strings for command line arguments
            char idStr[10], capStr[10], durStr[10];
            sprintf(idStr, "%d", i);
            sprintf(capStr, "%d", capacity);
            sprintf(durStr, "%d", duration);

            printf("Starting Factory %d with capacity %d and duration %d\n", 
                   i, capacity, duration);

            execlp("./factory", "factory", idStr, capStr, durStr, NULL);
            perror("Failed to execute factory");
            exit(EXIT_FAILURE);
        }
        child_pids[i] = factoryPid;
    }
    Sem_wait(finished_sem);
    
    // Simulate checking printer status
    printf("Checking printer status...\n");
    Usleep(2000000);  // Sleep for 2 seconds
    
    // Signal supervisor to print final report
    Sem_post(print_report_sem);

    // Wait for all child processes
    for (int i = 0; i < num_factories; i++) {
        int status;
        waitpid(child_pids[i], &status, 0);
    }

    // Clean up resources
    cleanup();

    printf("Sales process completed successfully\n");
    return 0;

}

