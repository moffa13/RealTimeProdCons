#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include "logger.h"
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>
#include "pc-rt-common.h"

//#define PRINT_DEBUG

#define MAX_LINE_CHAR 256 + 1 + 1 // 100 chars + line return + null terminator.

static int maxThreads;
pthread_mutex_t printf_mutex;
static pthread_rwlock_t catalogLock;
static pthread_mutex_t threadsLock;
struct shmem *seg;
static int currentThreads = 0;
static mqd_t queue;

struct LinkedList *allThreads;

void receiveQueue();
void *production(void*);

/*
Thread Safe
*/
void threadsListAdd(pthread_t *thread){
	pthread_mutex_lock(&threadsLock);
	linkedListAdd(allThreads, (void*)thread);
	currentThreads++;
	pthread_mutex_unlock(&threadsLock);
}

BOOLEAN thread_compare(void* a, void* b){
	return pthread_equal(*((pthread_t*)a), *((pthread_t*)b));
}

/*
Thread Safe
*/
void threadsListRemove(pthread_t thread){
	pthread_mutex_lock(&threadsLock);
	if(linkedListRemove(&allThreads, &thread, thread_compare) == TRUE)
		currentThreads--;
	pthread_mutex_unlock(&threadsLock);
}

/*
Thread Safe
*/
int getCurrentThreads(){
	int r;
	pthread_mutex_lock(&threadsLock);
	r = currentThreads;
	pthread_mutex_unlock(&threadsLock);
	return r;
}

void deleteMessageQueue(){
	char name[256];
	sprintf(name, "%s-%d", QUEUE_NAME_BASE, getpid());
    if(mq_unlink(name) == -1){
        perror("Suppression file de messages");
    }
}

void makeMessageQueue(){
	struct mq_attr attrs;
	attrs.mq_curmsgs = 0;
	attrs.mq_flags = 0;
	attrs.mq_msgsize = sizeof(struct producerRequest);
	attrs.mq_maxmsg = MAX_ORDERS_IN_QUEUE;
	char name[256];
	sprintf(name, "%s-%d", QUEUE_NAME_BASE, getpid());
	queue = mq_open(name, O_CREAT | O_TRUNC | O_RDONLY, 0644, &attrs);
    
    if(queue == -1){
		if(errno == EEXIST){
			deleteMessageQueue();
			makeMessageQueue();
			return;
		}else if(errno == EINVAL){
			fprintf(stderr, "Impossible de changer le nombre max de message, lancer en root.\n");
			exit(3);
		}
		perror("Création file de messages");
		exit(3);
	}
}

void checkArgs(int argc){
	if(argc != 3){
		printf("Usage : ./pc-rc-prod <nbmax_TPr> <fichier_catalogue>\n");
		exit(1);
	}
}

int doesProducerMakeArticle(struct article* currArt){
	for(unsigned j = 0; j < currArt->currentProducers; j++){
		pid_t pid = currArt->producers[j].producer;
		if(pid == getpid()){ // We already have our pid into the producers list
			return j;
		}
	}
	return -1;
}


/*
Thread Safe
*/
void insertNewArticle(struct article art, float price, int articleProductionDelay){
	pthread_rwlock_wrlock(&catalogLock);
	BOOLEAN foundArticle = FALSE;
	for(int i = 0; i < seg->articleNo; i++){
		struct article *currArt = &(seg->articles[i]);
		if(strcmp(currArt->identification, art.identification) == 0){ // We found an already existing article
			BOOLEAN foundpid = doesProducerMakeArticle(currArt);
			foundArticle = TRUE;
			if(foundpid == -1){
				#ifdef PRINT_DEBUG
				pthread_mutex_lock(&printf_mutex);
				printf("Ajout nouveau producteur pour un article\n");
				pthread_mutex_unlock(&printf_mutex);
				#endif
				articleAddPid(getpid(), currArt, price, articleProductionDelay);
			}
		}
	}

	if(foundArticle == FALSE){
		if(seg->articleNo < MAX_ARTICLES){
			#ifdef PRINT_DEBUG
			pthread_mutex_lock(&printf_mutex);
			printf("Nouvel article ajouté\n");
			pthread_mutex_unlock(&printf_mutex);
			#endif
			articleAddPid(getpid(), &art, price, articleProductionDelay);
			memcpy(&(seg->articles[seg->articleNo]), &art, sizeof(struct article));
			seg->articleNo++;
		}else{
			#ifdef PRINT_DEBUG
			pthread_mutex_lock(&printf_mutex);
			printf("Aucun espace libre\n");
			pthread_mutex_unlock(&printf_mutex);
			#endif
		}
	}
	pthread_rwlock_unlock(&catalogLock);
}

/*
Thread Safe
*/
struct artValid getArticle(int artId){
	struct artValid package;
	package.valid = FALSE;
	pthread_rwlock_wrlock(&catalogLock);
	struct article art = seg->articles[artId];
	int pid;
	if((pid = doesProducerMakeArticle(&art)) != -1){ // The process can make the product
		package.valid = TRUE;
		package.art = art;
		package.pid = pid;
	}
	pthread_rwlock_unlock(&catalogLock);
	return package;
}

void receiveQueue(){
	while(TRUE){ // While there is available threads, block on receive
		if(getCurrentThreads() < maxThreads){
			
			struct producerRequest *msg = malloc(sizeof(struct producerRequest));
			ssize_t sz = mq_receive(queue, (char*)msg, sizeof(struct producerRequest), NULL);
			if(sz == -1){
				perror("RECEIVE MESSAGE");
			}else{
				pthread_t *th = malloc(sizeof(pthread_t));
				threadsListAdd(th);
				pthread_create(th, NULL, production, (void*)msg);
			}
		}else{
			usleep(1000);
		}
	}
}

int getRandProdTime(int prodTime){
	int percent = prodTime * PRODUCTION_DELAY_DIFF_PERCENT;
	int a = -percent;
	int b = percent;
	int r = random() % (b - a) + a;
	return prodTime + r;
}

void sendProdToCons(struct producerRequest *req){
	union sigval sv;
	sv.sival_int = req->orderId;
	sigqueue(req->customer, PROD_MADE_SIG, sv);
}

void *production(void *a){
	struct producerRequest *req = (struct producerRequest*)a;
	struct artValid art = getArticle(req->artId);
	if(!art.valid){
		#ifdef PRINT_DEBUG
		pthread_mutex_lock(&printf_mutex);
		printf("The producer does not make that article, aborting.\n");
		pthread_mutex_unlock(&printf_mutex);
		#endif
	}else{
		logIt(PROD_COMM, "%d\n", req->artId, 1, req->customer);
		int realTime = getRandProdTime(art.art.producers[art.pid].productionDelayMs);
		usleep(realTime * 1000);
		sendProdToCons(req);
		logIt(PROD_PROD, "%d %d\n", req->artId, 2, realTime, req->customer);
	}

	free(req);
	pthread_t me = pthread_self();
	threadsListRemove(me);

	return NULL;
}

void readCatalogue(const char* file){
	FILE *f = fopen(file, "r"); // Opens catalogue in reading mode
	if(f == NULL){
		fprintf(stderr, "Error reading catalogue file.\n");
		exit(2);
	}

	char line[MAX_LINE_CHAR] = {0};

	int lineNo = -1;

	struct article art;
	float price;
	int productionDelayMs;

	while(fgets(line, MAX_LINE_CHAR, f)){

		lineNo++;

		char last = line[MAX_LINE_CHAR - 2];
		if(last != '\0' && last != '\n'){ // Line is larger than 256 chars
			fprintf(stderr, "Error, line contains too much characters.\n");
			exit(2);
		}

		switch (lineNo)
		{
			case 0:
				strcpy(art.identification, line);
				break;
			case 1:
				strcpy(art.description, line);
				break;
			case 2:
				price = atof(line);
				break;
			case 3:
				productionDelayMs = atoi(line);
				break;
		}

		if(lineNo == 3){
			insertNewArticle(art, price, productionDelayMs);
			lineNo = -1;
		}

		
	}

	fclose(f);

}

void sigintHandler(int sig){
	#ifdef PRINT_DEBUG
	printf("Destruction de la file de message et détachement du segment.\n");
	#endif
	unloadSegment();
	deleteMessageQueue();
	free(allThreads);
	exit(0);
}

void initSignals(){

	sigset_t mask;
	sigfillset(&mask);
	sigdelset(&mask, SIGALRM);
	sigdelset(&mask, SIGINT);

	sigprocmask(SIG_SETMASK, &mask, NULL);

	struct sigaction s;
	s.sa_handler = sigintHandler;
	s.sa_flags = 0;
	sigemptyset(&s.sa_mask);
	
	sigaction(SIGINT, &s, NULL);
}

void initLock(){
	pthread_rwlock_init(&catalogLock, NULL);
	pthread_mutex_init(&printf_mutex, NULL);
	pthread_mutex_init(&threadsLock, NULL);
}

void deInitLock(){
	pthread_rwlock_destroy(&catalogLock);
	pthread_mutex_destroy(&printf_mutex);
	pthread_mutex_destroy(&threadsLock);
}

int main(int argc, char* argv[]){
	checkArgs(argc);
	seg = checkForSegment();
	maxThreads = atoi(argv[1]);
	allThreads = malloc(sizeof(struct LinkedList));
	allThreads->elem = NULL;
	allThreads->next = NULL;
	srandom(time(NULL));
	initLock();
	makeMessageQueue();
	readCatalogue(argv[2]);
	initSignals();
	receiveQueue();
}
