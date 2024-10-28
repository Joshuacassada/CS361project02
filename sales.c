#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include "wrappers.h"
#include "message.h"
#include "shmem.h"

// Global variables for cleanup
int shmid = -1;
int msgid = -1;
sem_t *factoryLogSem = NULL;
sem_t *supDoneSem = NULL;
sem_t *printReportSem = NULL;
shData *sharedData = NULL;
pid_t *childPids = NULL;
int numFactories;

// Semaphore names
char factoryLogSemName[256];
char supDoneSemName[256];
char printReportSemName[256];

void cleanup() {
    // Detach and remove shared memory
    if (sharedData)
        Shmdt(sharedData);
    if (shmid >= 0)
        shmctl(shmid, IPC_RMID, NULL);
    
    // Remove message queue
    if (msgid >= 0)
        msgctl(msgid, IPC_RMID, NULL);
    
    // Close and unlink semaphores
    if (factoryLogSem) {
        Sem_close(factoryLogSem);
        sem_unlink(factoryLogSemName);
    }
    if (supDoneSem) {
        Sem_close(supDoneSem);
        sem_unlink(supDoneSemName);
    }
    if (printReportSem) {
        Sem_close(printReportSem);
        sem_unlink(printReportSemName);
    }
    
    // Free allocated memory
    if (childPids)
        free(childPids);
}

void sigHandler(int sig) {
    printf("\nSALES: Received signal %d. Cleaning up...\n", sig);
    
    // Kill all child processes
    if (childPids) {
        for (int i = 0; i < numFactories + 1; i++) {
            if (childPids[i] > 0) {
                kill(childPids[i], SIGKILL);
            }
        }
    }
    
    cleanup();
    exit(1);
}

void setupEnvironment() {
    setenv("FACTORY_LOG_SEM", factoryLogSemName, 1);
    setenv("SUPERVISOR_DONE_SEM", supDoneSemName, 1);
    setenv("PRINT_REPORT_SEM", printReportSemName, 1);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <num_factories> <order_size>\n", argv[0]);
        exit(1);
    }
    
    numFactories = atoi(argv[1]);
    int orderSize = atoi(argv[2]);
    
    if (numFactories > MAXFACTORIES || numFactories < 1) {
        fprintf(stderr, "Number of factories must be between 1 and %d\n", MAXFACTORIES);
        exit(1);
    }

    // Create unique semaphore names using pid to avoid conflicts
    pid_t pid = getpid();
    snprintf(factoryLogSemName, sizeof(factoryLogSemName), "/sem_fac_%d", pid);
    snprintf(supDoneSemName, sizeof(supDoneSemName), "/sem_sup_%d", pid);
    snprintf(printReportSemName, sizeof(printReportSemName), "/sem_print_%d", pid);
    
    // Clean up any existing semaphores
    sem_unlink(factoryLogSemName);
    sem_unlink(supDoneSemName);
    sem_unlink(printReportSemName);
    
    // Set up signal handlers
    sigactionWrapper(SIGINT, sigHandler);
    sigactionWrapper(SIGTERM, sigHandler);
    
    // Allocate child PIDs array
    childPids = malloc((numFactories + 1) * sizeof(pid_t));
    
    // Create shared memory
    key_t shmkey = ftok(".", pid);
    shmid = Shmget(shmkey, SHMEM_SIZE, IPC_CREAT | 0666);
    sharedData = (shData *)Shmat(shmid, NULL, 0);
    
    // Initialize shared data
    sharedData->order_size = orderSize;
    sharedData->made = 0;
    sharedData->remain = orderSize;
    sharedData->activeFactories = numFactories;
    
    // Create message queue
    key_t msgkey = ftok(".", pid + 1);
    msgid = Msgget(msgkey, IPC_CREAT | 0666);
    
    // Create semaphores
    factoryLogSem = Sem_open(factoryLogSemName, O_CREAT, 0666, 1);
    supDoneSem = Sem_open(supDoneSemName, O_CREAT, 0666, 0);
    printReportSem = Sem_open(printReportSemName, O_CREAT, 0666, 0);
    
    printf("SALES: Will Request an Order of Size = %d parts\n", orderSize);
    printf("Creating %d Factory(ies)\n", numFactories);
    
    // Create supervisor process
    pid_t supPid = Fork();
    if (supPid == 0) {
        // Child process (Supervisor)
        char numFactStr[10];
        sprintf(numFactStr, "%d", numFactories);
        
        setupEnvironment();
        
        // Redirect stdout to supervisor.log
        freopen("supervisor.log", "w", stdout);
        execl("./supervisor", "supervisor", numFactStr, NULL);
        perror("execl supervisor failed");
        exit(1);
    }
    childPids[0] = supPid;
    
    // Seed random number generator
    srandom(time(NULL));
    
    // Create factory processes
    for (int i = 0; i < numFactories; i++) {
        int capacity = 10 + (random() % 41); // 10 to 50
        int duration = 500 + (random() % 701); // 500 to 1200
        
        pid_t factoryPid = Fork();
        if (factoryPid == 0) {
            // Child process (Factory)
            char idStr[10], capStr[10], durStr[10];
            sprintf(idStr, "%d", i + 1);
            sprintf(capStr, "%d", capacity);
            sprintf(durStr, "%d", duration);
            
            setupEnvironment();
            
            // Redirect stdout to factory.log
            freopen("factory.log", "a", stdout);
            execl("./factory", "factory", idStr, capStr, durStr, NULL);
            perror("execl factory failed");
            exit(1);
        }
        childPids[i + 1] = factoryPid;
        
        printf("SALES: Factory # %3d was created, with Capacity=%4d and Duration=%4d\n", 
               i + 1, capacity, duration);
    }
    
    // Wait for supervisor to signal completion
    Sem_wait(supDoneSem);
    printf("SALES: Supervisor says all Factories have completed their mission\n");
    
    // Simulate checking printer status
    sleep(2);
    
    // Signal supervisor to print final report
    printf("SALES: Permission granted to print final report\n");
    Sem_post(printReportSem);
    
    // Wait for all child processes
    printf("SALES: Cleaning up after the Supervisor Factory Processes\n");
    for (int i = 0; i < numFactories + 1; i++) {
        wait(NULL);
    }
    
    // Clean up resources
    cleanup();
    return 0;
}