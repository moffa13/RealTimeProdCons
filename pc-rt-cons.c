#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <mqueue.h>
#include <unistd.h>
#include <errno.h>
#include "logger.h"
#include "pc-rt-common.h"

//#define PRINT_DEBUG

static int nbArt;
static struct shmem *seg;
static pthread_rwlock_t lock;
static pthread_mutex_t orders_mutex;
static struct LinkedList *orders;
static int ordNum = 0;
static int nbThreads = 0;

pthread_mutex_t printf_mutex;

void checkArgs(int argc){
	if(argc != 2){
		printf("Usage : ./pc-rc-cons <nbart>\n");
		exit(1);
	}
}

// Thread Safe
// Returns the index in the segment of a random article.
// -1 if there is no article
int getRandomArticle(){
	pthread_rwlock_rdlock(&lock);
	if(seg->articleNo == 0) return -1;
	long r = random() % seg->articleNo;
	pthread_rwlock_unlock(&lock);
	return r;
}


/*
	Picks a random producer given an article index r
	Only returns the one with lowest price and if equal, lower production delay.
	Tries to return a producer with a pid different than <avoid>
*/
struct articleDetail getProducer(int r, pid_t avoid){
	struct article *art = &(seg->articles[r]);
	struct articleDetail winner = {0};
	BOOLEAN found = FALSE;
	for(unsigned i = 0; i < art->currentProducers; i++){
		pid_t pid = art->producers[i].producer;
		if(kill(pid, 0) == 0){ // Producer still exist
			if(found == FALSE ||
			   winner.producer == avoid || // Current producer is the one we want to skip for now
			   (pid != avoid && (art->producers[i].price < winner.price || // Price is less
			   (art->producers[i].price == winner.price && art->producers[i].productionDelayMs < winner.productionDelayMs))) // Price equal but prod delay is smaller
			){
				winner = art->producers[i];
				found = TRUE;
			}
		}
	}

	return winner;
}

/**
 * Sends the order to the corresponding producer.
 * Returns FALSE is the queue couldn't be open
 * Or if in some way the message couldn't be sent
*/
BOOLEAN sendToProd(struct order *ord){
	char name[256];
	sprintf(name, "%s-%d", QUEUE_NAME_BASE, ord->art.producer);
	mqd_t queue = mq_open(name, O_WRONLY | O_NONBLOCK , 0644, NULL); // Open the queue in write only and non blocking mode
	if(queue == -1){
		perror("Envoi message");
		return FALSE;
	}
	struct producerRequest req;
	req.customer = getpid();
	req.artId = ord->artId;
	req.orderId = ord->orderId;

	BOOLEAN success = TRUE;

	struct timeval now;

	while(TRUE){
		gettimeofday(&now, NULL);
		ord->timestamp = now; // Always set the timestamp in case of big delay to be more accurate
		if(mq_send(queue, (const char*)&req, sizeof(struct producerRequest), 10) == -1){
			if(errno != EAGAIN){ // Error is something else than a full queue
				perror("Envoi message");
				success = FALSE;
				break;
			}else{
				usleep(1000); // Wait a bit to check again if we can now send the message through the queue
			}
		}else{
			break; // Message sent
		}
	}

	mq_close(queue);

	return success;
}



void *threadHandle(void* a){

	sigset_t s;
	sigfillset(&s);
	pthread_sigmask(SIG_SETMASK, &s, NULL);

	struct order *bestProd = (struct order *)a;

	pthread_mutex_lock(&orders_mutex);

	linkedListAdd(orders, bestProd);
	sendToProd(bestProd);

	nbThreads--;

	pthread_mutex_unlock(&orders_mutex);
	
	return NULL;
}

void makeThread(pthread_t *thread, struct order* order){

	nbThreads++;
	int err = pthread_create(thread, NULL, threadHandle, order);

	if(err != 0){
		pthread_mutex_lock(&printf_mutex);
		fprintf(stderr, "Error creating Thread (%d)", err);
		pthread_mutex_unlock(&printf_mutex);
	}
}

void createThreads(){

	if(seg->articleNo == 0){
		#ifdef PRINT_DEBUG
		pthread_mutex_lock(&printf_mutex);
		printf("There is no article\n");
		pthread_mutex_unlock(&printf_mutex);
		#endif
		exit(0);
	}

	for(int i = 0; i < nbArt; i++){
		struct order *order = malloc(sizeof(struct order));
		int randomArticle = getRandomArticle();
		struct articleDetail newArt = getProducer(randomArticle, 0);

		if(newArt.producer != 0) {
			order->art = newArt;
			order->orderId = ordNum++;
			order->artId = randomArticle;
			logIt(CONS_COMM, "%d %d\n", order->artId, 2, order->art.productionDelayMs, order->art.producer);
			pthread_mutex_lock(&orders_mutex);
			pthread_t t;
			makeThread(&t, order);
			pthread_mutex_unlock(&orders_mutex);
		}else{
			#ifdef PRINT_DEBUG
			pthread_mutex_lock(&printf_mutex);
			printf("No producer for that article.\n");
			pthread_mutex_unlock(&printf_mutex);
			#endif
		}

		
	}

}

void initLock(){
	pthread_rwlock_init(&lock, NULL);
	pthread_mutex_init(&printf_mutex, NULL);
	pthread_mutex_init(&orders_mutex, NULL);
}


void deInitLock(){
	pthread_rwlock_destroy(&lock);
	pthread_mutex_destroy(&printf_mutex);
	pthread_mutex_destroy(&orders_mutex);
}

void initTimer(){
	struct itimerval itvl;
	itvl.it_interval.tv_sec = 0; itvl.it_interval.tv_usec = 1000;
	itvl.it_value.tv_sec = 0; itvl.it_value.tv_usec = 1000;
	setitimer(ITIMER_REAL, &itvl, NULL);
}

unsigned long timevalToTimestamp(struct timeval timestamp){
	unsigned long timestampL = timestamp.tv_sec * 1000 + timestamp.tv_usec / 1000;
	return timestampL;
}

BOOLEAN isDelayed(struct timeval timestamp, int prodDelay){
	int allowedDiff = prodDelay * PRODUCTION_DELAY_MAX_DIFF_PERCENT;
	struct timeval now;
	gettimeofday(&now, NULL);
	unsigned long nowL = timevalToTimestamp(now);
	unsigned long timestampL = timevalToTimestamp(timestamp);
	if(nowL > timestampL + prodDelay + allowedDiff){
		return TRUE;
	}
	return FALSE;
}

BOOLEAN orderCompareFunc(void *a, void *b){
	struct order* ordA = (struct order*)a;
	struct order* ordB = (struct order*)b;
	return ordA->orderId == ordB->orderId ? TRUE : FALSE; 
}

void timerTrigger(int sig){
	pthread_mutex_lock(&orders_mutex);
	struct LinkedList *curr = orders;

	BOOLEAN hasADelayedOrder = FALSE;

	while(curr != NULL && curr->elem != NULL){
		struct order* art = (struct order*)curr->elem;
		if(isDelayed(art->timestamp, art->art.productionDelayMs) == TRUE){
			hasADelayedOrder = TRUE;
			logIt(CONS_DELAY, "%d\n", art->artId, 1, art->art.producer);
			linkedListRemove(&orders, art, orderCompareFunc);
			
			struct order *order = malloc(sizeof(struct order));
			struct articleDetail newArt = getProducer(art->artId, art->art.producer);

			struct timeval now;
			gettimeofday(&now, NULL);
			order->timestamp = now;
			order->art = newArt;
			order->orderId = ordNum++;
			order->artId = art->artId;

			logIt(CONS_RENEW, "%d %d\n", order->artId, 2, newArt.productionDelayMs, newArt.producer);
			pthread_t thread;
			makeThread(&thread, order);
			
		}
		curr = curr->next;
	}

	if(hasADelayedOrder == FALSE && nbThreads == 0 && orders->elem == NULL){ // No delayed order (no new thread), and orders list is empty
		deInitLock();
		unloadSegment();
		exit(0);
	}

	pthread_mutex_unlock(&orders_mutex);
}



void orderReceivedHandler(int sig, siginfo_t *info, void *unused){
	pthread_mutex_lock(&orders_mutex);
	struct LinkedList *curr = orders;
	
	while(curr != NULL && curr->elem != NULL){ // Iterate through all orders
		struct order *currOrd = (struct order*)curr->elem;
		if(currOrd->orderId == info->si_value.sival_int && 
		   !isDelayed(currOrd->timestamp, currOrd->art.productionDelayMs)
		){ // Order id received in the signal has been found
		   // And don't handle the order if delay, the timer will take care of it
			struct timeval now;
			gettimeofday(&now, NULL);
			logIt(CONS_REC, "%d %d\n", currOrd->artId, 2, timevalToTimestamp(now) - timevalToTimestamp(currOrd->timestamp), currOrd->art.producer);
			linkedListRemove(&orders, currOrd, orderCompareFunc);
		}
		curr = curr->next;
	}
	
	pthread_mutex_unlock(&orders_mutex);
}

void sigintHandler(int sig){
	#ifdef PRINT_DEBUG
	printf("Détachement du segment.\n");
	#endif
	unloadSegment();
	exit(0);
}

void initSignals(){

	sigset_t mask;
	sigfillset(&mask);
	sigdelset(&mask, PROD_MADE_SIG);
	sigdelset(&mask, SIGALRM);
	sigdelset(&mask, SIGINT);

	// Allow sigalrm, sigint and custom sig
	sigprocmask(SIG_SETMASK, &mask, NULL);


	struct sigaction s;
	sigfillset(&s.sa_mask); // Nothing can interrupt signals below except sigint
	sigdelset(&s.sa_mask, SIGINT);

	
	s.sa_handler = timerTrigger;
	s.sa_flags = 0;
	sigaction(SIGALRM, &s, NULL);

	s.sa_handler = 0;
	s.sa_flags = SA_SIGINFO;
	s.sa_sigaction = orderReceivedHandler;
	sigaction(PROD_MADE_SIG, &s, NULL);

	signal(SIGINT, sigintHandler);
}

int main(int argc, char* argv[]){
	srandom(time(NULL));
	checkArgs(argc);
	nbArt = atoi(argv[1]);
	seg = checkForSegment();
	orders = malloc(sizeof(struct LinkedList));
	initLock();
	initSignals();
	createThreads();
	initTimer();
	while(1) pause();
}