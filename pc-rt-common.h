#ifndef RT_COMMON_DEF
#define RT_COMMON_DEF

#include <sys/types.h>

#define PROD_MADE_SIG SIGRTMIN
#define SHARED_KEY_FILE "/bin/mv"
#define SHARED_KEY_UID 745673557
#define QUEUE_NAME_BASE "/RTCPQUEUE"
#define ARTICLE_ID_MAX_SIZE 25
#define MAX_ORDERS_IN_QUEUE 50
#define ARTICLE_DESC_MAX_SIZE 100
#define ARTICLE_MAX_PRODUCERS 20
#define PRODUCTION_DELAY_DIFF_PERCENT 0.15
#define PRODUCTION_DELAY_MAX_DIFF_PERCENT 0.05
#define MAX_ARTICLES 256
#define BOOLEAN int
#define FALSE 0
#define TRUE 1

#define COMPARE_FUNC(x) BOOLEAN (*x) (void *a, void *b)

size_t getSegmentSize();

int createShared();

struct shmem *getSegment();
void unloadSegment();

struct shmem * checkForSegment();

struct producerRequest{
    pid_t customer;
    int orderId;
    int artId;
};

struct articleDetail{
    pid_t producer;
    float price;
    unsigned int productionDelayMs;
};

struct order{
    struct timeval timestamp;
    struct articleDetail art;
    int orderId;
    int artId;
};

struct article {
    char identification[ARTICLE_ID_MAX_SIZE + 1];
    char description[ARTICLE_DESC_MAX_SIZE + 1];
    unsigned int currentProducers;
    struct articleDetail producers[ARTICLE_MAX_PRODUCERS];
};

struct artValid {
    BOOLEAN valid;
    struct article art;
    int pid;
};

struct LinkedList {
    void *elem;
	struct LinkedList *next;
};

BOOLEAN articleAddPid(pid_t pid, struct article *art, float price, int articleProductionDelay);

BOOLEAN linkedListRemove(struct LinkedList **l, void *e, COMPARE_FUNC(f));
void linkedListAdd(struct LinkedList *l, void *e);

struct shmem {
    int articleNo;
    struct article articles[MAX_ARTICLES];
};

#endif