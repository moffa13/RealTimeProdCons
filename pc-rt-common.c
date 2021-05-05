#include <sys/ipc.h>
#include <sys/shm.h>
#include "pc-rt-common.h"
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

int createShared(int attr){
    key_t key = ftok(SHARED_KEY_FILE, SHARED_KEY_UID);
    int id = shmget(key, getSegmentSize(), attr | 0644);
    return id;
}

struct shmem *getSegment(){
    int id = createShared(0);
    if(id == -1){
        return NULL;
    }
    struct shmem * seg = shmat(id, NULL, 0);
    if((uintptr_t)seg == (uintptr_t)-1) return NULL;
    return seg;
}

void linkedListAdd(struct LinkedList *l, void *e){
    if(l->elem == NULL){
		l->elem = e;
	}else{
		struct LinkedList *current = l;
		struct LinkedList *prev;
		while(current != NULL){
			prev = current;
			current = current->next;
		}

		struct LinkedList *tmp = malloc(sizeof(struct LinkedList));
		prev->next = tmp;
		tmp->next = NULL;
		tmp->elem = e;
	}
}

BOOLEAN linkedListRemove(struct LinkedList **li, void *e, COMPARE_FUNC(f)){

    struct LinkedList *l = *li;

    struct LinkedList *current = l;
	struct LinkedList *prev = NULL;
	while(current != NULL && !f(current->elem, e)){
		prev = current;
		current = current->next;
	}

	if(current != NULL){ // Elem is found
		free(current->elem);
		if(prev != NULL){ // There is a previous elem
			prev->next = current->next;
			free(current);
		}else{	// No previous elem
			if(current->next == NULL){
				current->elem = NULL;
			}else{
				*li = current->next;
				free(current);
			}
		}
		return TRUE;
	}

    return FALSE;
}

void unloadSegment(){
    struct shmem * seg = getSegment();
    if(seg != NULL){
        shmdt(seg);
    }
}

BOOLEAN articleAddPid(pid_t pid, struct article *art, float price, int articleProductionDelay){
    BOOLEAN added = FALSE;
    for(unsigned i = 0; i < art->currentProducers; i++){ // Iterate each producer of that article
        pid_t currPid = art->producers[i].producer;
        if(kill(currPid, 0) != 0){ // No pid is assigned (maybe deleted)
            art->producers[i].producer = pid; // Assign it
            art->producers[i].price = price;
            art->producers[i].productionDelayMs = articleProductionDelay;
            added = TRUE;
            break;
        }
    }
    if(added == FALSE){ // All pids until currentProducers are assigned
        if(art->currentProducers < ARTICLE_MAX_PRODUCERS){ // Some remaining space
            art->producers[art->currentProducers].producer = pid;
            art->producers[art->currentProducers].price = price;
            art->producers[art->currentProducers].productionDelayMs = articleProductionDelay;
            art->currentProducers++;
        }else{ // No space left
            return FALSE;
        }
    }
    return TRUE;
}

/*BOOLEAN articleRemovePid(pid_t pid, struct article *art){
    for(int i = 0; i < art->currentProducers; i++){ // Iterate each producer of that article
        pid_t currPid = art->producers[i];
        if(currPid == pid){ // Found pid
            art->producers[i] = 0; // Remove it
            return TRUE;
        }
    }
    return FALSE;
}*/

size_t getSegmentSize(){
    return sizeof(struct shmem);
}

struct shmem *checkForSegment(){
    struct shmem *seg = getSegment();
    if(seg == NULL){
		printf("Veuillez lancer le service avec ./pc-rt-start\n");
        perror("Obtention segment");
		exit(4);
	}
    return seg;
}
