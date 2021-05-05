#ifndef LOGGER_H
#define LOGGER_H

#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>

extern pthread_mutex_t printf_mutex;

typedef enum {
    PROD_COMM,
    PROD_PROD,
    CONS_COMM,
    CONS_REC,
    CONS_DELAY,
    CONS_RENEW
} LogType_t;

void logIt(LogType_t what, const char* format, int artId, int num, ...);

#endif
