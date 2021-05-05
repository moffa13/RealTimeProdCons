#include "pc-rt-common.h"
#include <errno.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

int main(){
    int id = createShared(0);
    if(id == -1){
        if(errno == ENOENT){
            printf("Le service n'est pas lancé. Démarrer le service avec ./pc-rt-start\n");
            exit(0);
        }else{
            perror("Obtention segment");
            exit(3);
        }
    }

    int rep = shmctl(id, IPC_RMID, NULL);
    if(rep != -1){
        printf("Service stoppé. Démarrer le service avec ./pc-rt-start\n");
    }else{
        perror("Destruction segment");
    }

    return 0;
}