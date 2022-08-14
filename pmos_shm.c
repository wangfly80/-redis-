#include "pmos_shm.h"

#ifdef WIN32
int DetachShm(SHMObj* shmobj)
{
    if (shmobj->m_addr != NULL)
    {
        if (!UnmapViewOfFile(shmobj->m_addr))
        {
            return -1;
        }
        shmobj->m_addr = NULL;
        CloseHandle(shmobj->m_shmid);
        shmobj->m_shmid = NULL;
    }

    return 0;
}

int AttachShm(SHMObj* shmobj)
{
    char str_key[128];

    sprintf(str_key, "%x", shmobj->m_key);

    shmobj->m_shmid = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, shmobj->size, str_key);

    if (shmobj->m_shmid == NULL)
    {
        printf("AttachShm: CreateFileMapping error with %ld, size=%d, str_key=%s\n",
               GetLastError(), shmobj->size, str_key);
        return -1;
    }
    shmobj->m_addr = (char*)MapViewOfFile(shmobj->m_shmid, FILE_MAP_WRITE|FILE_MAP_READ, 0, 0, 0);
    if(shmobj->m_addr == NULL)
    {
        printf("MapViewOfFile error: %ld\n", GetLastError());
        return -1;
    }

    return 0;
}

int InitSem(SEMObj *semobj)
{
    char str_key[128];

    if (semobj->m_key == 0) return -1;

    sprintf(str_key, "sem%x", semobj->m_key);

    semobj->m_semID = CreateMutex(NULL, FALSE, str_key);
    if (semobj->m_semID == NULL)
    {
        printf("CreateSemaphore in InitSem error with GetLastError = %ld\n", GetLastError());
        return -1;
    }
    return 0;
}

int P(SEMObj *semobj)
{
    int ret_code;

    if(semobj->m_semID == NULL)
    {
        printf("%ld sem_p called before init\n", GetCurrentProcessId());
        return -1;
    }
    ret_code = WaitForSingleObject(semobj->m_semID,  INFINITE);
    if ((ret_code == WAIT_OBJECT_0) || (ret_code == WAIT_ABANDONED))
    {
        return 0;
    }

    printf("sem_p ret[%d] \n", ret_code);
    return -1;
}

int V(SEMObj *semobj)
{
    if(semobj->m_semID == NULL)
    {
        printf("%ld sem_v called before init\n", GetCurrentProcessId());
        return -1;
    }

    if (!ReleaseMutex(semobj->m_semID))
        return -1;

    return 0;
}

#else
int DetachShm(SHMObj* shmobj)
{
    int ret = shmdt(shmobj->m_addr);
    if (ret < 0)
        return -1;
    else
    {
        shmobj->m_addr = NULL;
        return 0;
    }
}

int AttachShm(SHMObj* shmobj)
{
    shmobj->m_shmid = shmget(shmobj->m_key, 0, 0666);
    if (shmobj->m_shmid < 0)
    {
        if (errno != ENOENT)
        {
            printf("AttachShm: shmget 1 error with errno = %d", errno);
            return -1;
        }
        if (shmobj->size == 0)
        {
            printf("size is 0\n");
            return -1;
        }
        else
        {
            shmobj->m_shmid = shmget(shmobj->m_key, shmobj->size, 0666|IPC_CREAT|IPC_EXCL);
            if (shmobj->m_shmid < 0)
            {
                printf("AttachShm: shmget error with errno = %d\n", errno);
                return -1;
            }
        }
    }
    else
    {
        struct shmid_ds shmds;
        if (shmobj->size != 0)
        {
            if (shmctl(shmobj->m_shmid, IPC_STAT, &shmds) < 0)
            {
                printf("AttachShm: [%d] shmctl error with errno = %d\n", shmobj->m_shmid, errno);
                return -1;
            }
            if ((shmds.shm_segsz != shmobj->size))
            {
                printf("AttachShm: shmid[%d] share memory size error\n", shmobj->m_shmid);
                return -1;
            }
        }
    }

    shmobj->m_addr = (char*)shmat(shmobj->m_shmid, 0, 0);

    return 0;
}

int InitSem(SEMObj *semobj)
{
    if (semobj->m_key == 0) return -1;

    semobj->m_semID = semget(semobj->m_key, 1, IPC_CREAT|IPC_EXCL|0666);
    if (semobj->m_semID < 0)
    {
        if (errno == EEXIST)
        {
            semobj->m_semID = semget(semobj->m_key, 0, 0);
            if (semobj->m_semID < 0) return -1;
            return 0;
        }
        else
        {
            printf("semget error in sem_init with errno = %d\n", errno);
            return -1;
        }
    }
    else
    {
        union semun init_value;
        init_value.val = 1;
        int status = semctl(semobj->m_semID, 0, SETVAL, init_value);
        if (status < 0)
        {
            printf("semctl error in sem_init with errno = %d\n", errno);
            return -1;
        }
        return 0;
    }
}

int P(SEMObj *semobj)
{
    struct sembuf p_buf;

    if(semobj->m_key == 0)
    {
        printf("%d sem_p called before init\n", getpid());
        return -1;
    }

    p_buf.sem_num = 0;
    p_buf.sem_op  = -1;
    p_buf.sem_flg = SEM_UNDO;

    if (semop(semobj->m_semID, &p_buf, 1) == -1)
    {
        printf("semop error in sem_p key=%d, m_semID=%d, errno=%d\n",
               semobj->m_key, semobj->m_semID, errno);
        return -1;
    }

    return 0;
}

int V(SEMObj *semobj)
{
    struct sembuf v_buf;

    if(semobj->m_key == 0)
    {
        printf("%d sem_v called before init\n", getpid());
        return -1;
    }

    v_buf.sem_num = 0;
    v_buf.sem_op  = 1;
    v_buf.sem_flg = SEM_UNDO;

    if (semop(semobj->m_semID, &v_buf, 1) == -1)
    {
        printf("semop error in sem_v key=%d, m_semID=%d, errno=%d\n",
               semobj->m_key, semobj->m_semID, errno);
        return -1;
    }

    return 0;
}

#endif
