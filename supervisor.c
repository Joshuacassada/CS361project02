#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include "wrappers.h"
#include "message.h"
#include "shmem.h"
#include <sys/stat.h>
#include <time.h>





int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <num_factories>\n", argv[0]);
        exit(1);
    }

    int activeFactories = atoi(argv[1]);
    if (activeFactories <= 0 || activeFactories > MAXFACTORIES) {
        fprintf(stderr, "Invalid number of factories. Must be between 1 and %d.\n", MAXFACTORIES);
        exit(1);
    }
    


}
