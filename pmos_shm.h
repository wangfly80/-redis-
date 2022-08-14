#ifndef _PMOS_SHM_H_
#define _PMOS_SHM_H_

#ifndef WIN32
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <sys/times.h>
#else
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <process.h>
#include <time.h>
#include <sys/timeb.h>
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct SHMObj
{
#ifdef WIN32
    HANDLE m_shmid;
#else
    int m_shmid;
#endif
    int size;
    unsigned int m_key;
    char *m_addr;
} SHMObj;

typedef struct SEMObj
{
#ifdef WIN32
    HANDLE m_semID;
#else
    int m_semID;
#endif
    unsigned int m_key;
} SEMObj;

#ifndef WIN32
union semun
{
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};
#endif

extern int DetachShm(SHMObj* shmobj);
extern int AttachShm(SHMObj* shmobj);
extern int InitSem(SEMObj *semobj);
extern int P(SEMObj *semobj);
extern int V(SEMObj *semobj);

#endif
