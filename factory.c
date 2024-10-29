#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "wrappers.h"
#include "message.h"
#include "shmem.h"
#include <fcntl.h>
#include <sys/stat.h>

sem_t *sem_factory_log;

int main(int argc, char *argv[]) {
    if (argc != 4)
    {
        fprintf(stderr, "Usage: %s <factory_id> <capacity> <duration>\n", argv[0]);
        exit(1);
    }

    int factory_id = atoi(argv[1]);
    int factory_cap = atoi(argv[2]);
    int duration = atoi(argv[3]);

    sem_t *factoryLogSemaphore = Sem_open2("/sem_factory_log", 0);

    key_t shmkey = ftok(".", getppid());
    int shmid = Shmget(shmkey, SHMEM_SIZE, 0666);
    shData *sharedData = (shData *)Shmat(shmid, NULL, 0);

    Sem_wait(factoryLogSemaphore);
    printf("Factory #%d: Started. My Capacity = %3d, in %4d milliseconds\n", factory_id, factory_cap, duration);
    fflush(stdout);
    Sem_post(factoryLogSemaphore);

    int iterations = 0;
    int total_by_me = 0;
    int amount_to_make = 0;
    key_t msgkey = ftok(".", getppid() + 1);
    int msgid = Msgget(msgkey, 0666);

    while (sharedData->remain > 0)
    {
        if (sharedData->remain < factory_cap)
        {
            amount_to_make = sharedData -> remain;
        } else {
            amount_to_make = factory_cap;
        }
        Sem_wait(factoryLogSemaphore);
        sharedData->remain = sharedData->remain - amount_to_make;
        Sem_post(factoryLogSemaphore);

        Sem_wait(factoryLogSemaphore);
        printf("Factory #%d: Going to make %3d parts in %4d milliSecs", factory_id, amount_to_make, duration);
        fflush(stdout);
        Sem_post(factoryLogSemaphore);
        Usleep(2000);

        iterations++;
        total_by_me += amount_to_make;

        msgBuf message;
        message.mtype = 1;
        message.purpose = PRODUCTION_MSG;
        message.facID = factory_id;
        message.capacity = factory_cap;
        message.partsMade = amount_to_make;
        message.duration = duration;

        // Send the message
        if (msgsnd(msgid, &message, MSG_INFO_SIZE, 0) == -1) {
            perror("msgsnd failed");
            exit(1);
        }
    }
    msgBuf message;
    message.mtype = 1;
    message.purpose = COMPLETION_MSG;
    message.facID = factory_id;
    message.capacity = factory_cap;
    message.partsMade = total_by_me;
    message.duration = duration;

    if (msgsnd(msgid, &message, MSG_INFO_SIZE, 0) == -1) {
        perror("msgsnd failed");
        exit(1);
    }

    Sem_wait(factoryLogSemaphore);
    printf("Factory Line %d: Completed after making total of %d parts in %d iterations\n", factory_id, total_by_me, iterations);
    fflush(stdout);
    Sem_post(factoryLogSemaphore);
    return 0;
}