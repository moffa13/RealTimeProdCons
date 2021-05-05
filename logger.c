#include "logger.h"

void logIt(LogType_t what, const char* format, int artId, int num, ...){
    va_list vlist;
    va_start(vlist, num);
    pthread_mutex_lock(&printf_mutex);
    char *prefix;
    switch(what){
        case PROD_COMM:
        case CONS_COMM:
            prefix = "COMM";
            break;
        case PROD_PROD:
            prefix = "PROD";
            break;
        case CONS_REC:
            prefix = "REC";
            break;
        case CONS_DELAY:
            prefix = "DELAY";
            break;
        case CONS_RENEW:
            prefix = "RENEW";
            break;
    }
    printf("%s : %lu %d ", prefix, (unsigned long)time(NULL), artId);
    vprintf(format, vlist);
    pthread_mutex_unlock(&printf_mutex);
    va_end(vlist);
}