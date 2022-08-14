#ifndef _PMOS_UTILS_H_
#define _PMOS_UTILS_H_

#include <stdint.h>
#include <pthread.h>

#ifndef NULL
#define NULL (0)
#endif

enum
{
    PM_THREAD_START  = 0,
    PM_THREAD_STOP   = 1
};

typedef struct ThreadInfo
{
    pthread_t id;
    int       status;
} ThreadInfo;

#define APP_FREE(pVar) do {\
    if(pVar){ \
        free(pVar); \
        pVar = NULL; \
    }\
} while (0)

extern uint32_t crc32(const char *buf, uint32_t size);
extern uint64_t mstime(void);
extern int  isBigEndian();
extern void P_SWAP_16(uint16_t *value);
extern void P_SWAP_32(uint32_t *value);
extern void P_SWAP_64(uint64_t *value);
extern int  pmThreadCreate(ThreadInfo *thread,void *(* func)(void *),void *arg);
extern int  pmThreadStop(ThreadInfo *thread);
extern int  process_exist(int pid);
#endif
