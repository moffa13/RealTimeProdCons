#include <stdio.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <stdlib.h>
#include <memory.h>
#include <sys/stat.h>
#include <mqueue.h>
#include "pc-rt-common.h"

void makeSegment(){
    int id = createShared(IPC_CREAT | IPC_EXCL);
    
    if(id == -1){
        if(errno == EEXIST){
            printf("Service déjà lancé. Arrêter le service avec ./pc-rt-stop\n");
            exit(0);
        }else{
            perror("Creation segment : ");
            exit(3);
        }
    }

    struct shmem *seg = getSegment();

    if(seg != NULL){
        memset(seg, 0, getSegmentSize());
    }else{
        perror("Obtention segment");
        exit(3);
    }
}



int main(){

    makeSegment();

    printf("Service lancé. Arrêter le service avec ./pc-rt-stop\n");

    return 0;
}