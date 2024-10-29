#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "wrappers.h"
#include "message.h"
#include "shmem.h"

int main(int argc, char *argv[]) {
    if (argc != 4)
    {
        fprintf(stderr, "Usage: %s <factory_id> <capacity> <duration>\n", argv[0]);
        exit(1);
    } 
    int factory_id = atoi(argv[0]);
    int factory_cap = atoi(argv[1]);
    int duration = atoi(argv[2]);

    //get semaphore name
    sem_t *factoryLogSemaphore = Sem_open2("/mutex", 0);
    key_t shmkey = ftok("/shmem.h", getppid());
    return 0;
}