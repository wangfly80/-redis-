#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "pmos_event.h"
#include "pmos_select.c"

/*
 * 初始化事件处理器状态
 */
evEventLoop *evCreateEventLoop(int setsize)
{
    evEventLoop *eventLoop;
    int i;

    do
    {
        if ((eventLoop = malloc(sizeof(*eventLoop))) == NULL) break;

        eventLoop->events = malloc(sizeof(evFileEvent)*setsize);
        eventLoop->fired = malloc(sizeof(evFiredEvent)*setsize);
        eventLoop->timeEvents = malloc(sizeof(evTimeEvent)*EV_TIMEEVENT_SIZE);
        if (eventLoop->events == NULL
            || eventLoop->fired == NULL
            || eventLoop->timeEvents == NULL) break;

        eventLoop->setsize = setsize;
        eventLoop->lastTime = time(NULL);
        eventLoop->timeEventNextId = 0;
        eventLoop->stop = 0;
        eventLoop->maxfd = -1;
        eventLoop->beforesleep = NULL;
        if (evApiCreate(eventLoop) == -1) break;

        /* Events with mask == AE_NONE are not set. So let's initialize the
         * vector with it. */
        for (i = 0; i < setsize; i++)
        {
            eventLoop->events[i].mask = EV_NONE;
        }

        memset(eventLoop->timeEvents, 0, sizeof(evTimeEvent)*EV_TIMEEVENT_SIZE);

        return eventLoop;

    } while(0);

    if (eventLoop)
    {
        free(eventLoop->events);
        free(eventLoop->fired);
        free(eventLoop->timeEvents);
        free(eventLoop);
    }
    return NULL;
}

/* Return the current set size. */
int evGetSetSize(evEventLoop *eventLoop)
{
    return eventLoop->setsize;
}

/*
 * 删除事件处理器
 */
void evDeleteEventLoop(evEventLoop *eventLoop)
{
    evApiFree(eventLoop);
    free(eventLoop->events);
    free(eventLoop->fired);
    free(eventLoop->timeEvents);
    free(eventLoop);
}

/*
 * 停止事件处理器
 */
void evStop(evEventLoop *eventLoop)
{
    eventLoop->stop = 1;
}

/*
 * 根据 mask 参数的值，监听 fd 文件的状态，
 * 当 fd 可用时，执行 proc 函数
 */
int evCreateFileEvent(evEventLoop *eventLoop, int fd, int mask,
        evFileProc *proc, void *clientData)
{
    evFileEvent *fe = NULL;

    if (fd >= eventLoop->setsize)
    {
        return EV_ERROR;
    }

    fe = &eventLoop->events[fd];

    if (evApiAddEvent(eventLoop, fd, mask) == -1)
        return EV_ERROR;

    fe->mask |= mask;
    if (mask & EV_READABLE) fe->rfileProc = proc;
    if (mask & EV_WRITABLE) fe->wfileProc = proc;

    fe->clientData = clientData;

    if (fd > eventLoop->maxfd)
        eventLoop->maxfd = fd;

    return EV_OK;
}

/*
 * 将 fd 从 mask 指定的监听队列中删除
 */
void evDeleteFileEvent(evEventLoop *eventLoop, int fd, int mask)
{
    evFileEvent *fe = NULL;

    if (fd >= eventLoop->setsize) return;

    fe = &eventLoop->events[fd];
    if (fe->mask == EV_NONE) return;

    evApiDelEvent(eventLoop, fd, mask);

    fe->mask = fe->mask & (~mask);
    if (fd == eventLoop->maxfd && fe->mask == EV_NONE)
    {
        /* Update the max fd */
        int j;

        for (j = eventLoop->maxfd-1; j >= 0; j--)
            if (eventLoop->events[j].mask != EV_NONE) break;
        eventLoop->maxfd = j;
    }    
}

/*
 * 获取给定 fd 正在监听的事件类型
 */
int evGetFileEvents(evEventLoop *eventLoop, int fd)
{
    evFileEvent *fe = NULL;

    if (fd >= eventLoop->setsize) return 0;
    fe = &eventLoop->events[fd];

    return fe->mask;
}

/*
 * 取出当前时间的秒和毫秒，
 * 并分别将它们保存到 seconds 和 milliseconds 参数中
 */
static void evGetTime(long *seconds, long *milliseconds)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    *seconds = tv.tv_sec;
    *milliseconds = tv.tv_usec/1000;
}

/*
 * 在当前时间上加上 milliseconds 毫秒，
 * 并且将加上之后的秒数和毫秒数分别保存在 sec 和 ms 指针中。
 */
static void evAddMillisecondsToNow(long long milliseconds, long *sec, long *ms)
{
    long cur_sec, cur_ms, when_sec, when_ms;

    evGetTime(&cur_sec, &cur_ms);

    when_sec = cur_sec + milliseconds/1000;
    when_ms = cur_ms + milliseconds%1000;

    if (when_ms >= 1000)
    {
        when_sec ++;
        when_ms -= 1000;
    }

    *sec = when_sec;
    *ms = when_ms;
}

/*
 * 创建时间事件
 */
long long evCreateTimeEvent(evEventLoop *eventLoop, long long milliseconds,
        evTimeProc *proc, void *clientData,
        evEventFinalizerProc *finalizerProc)
{
    long long id = eventLoop->timeEventNextId++;
    int i;
    evTimeEvent *te = NULL;

    for (i=0; i<EV_TIMEEVENT_SIZE; i++)
    {
        if (!eventLoop->timeEvents[i].useFlag)
            break;
    }

    if (i >= EV_TIMEEVENT_SIZE) return EV_ERROR;

    te = &eventLoop->timeEvents[i];

    te->id = id;
    te->useFlag = 1;

    evAddMillisecondsToNow(milliseconds,&te->when_sec,&te->when_ms);
    te->timeProc = proc;
    te->finalizerProc = finalizerProc;
    te->clientData = clientData;

    return id;
}

/*
 * 删除给定 id 的时间事件
 */
int evDeleteTimeEvent(evEventLoop *eventLoop, long long id)
{
    evTimeEvent *te = NULL;
    int i;

    for (i=0; i<EV_TIMEEVENT_SIZE; i++)
    {
        if (eventLoop->timeEvents[i].id == id)
            break;
    }

    if (i >= EV_TIMEEVENT_SIZE) return EV_ERROR;

    te = &eventLoop->timeEvents[i];
    memset(te, 0, sizeof(evTimeEvent));

    return EV_OK;
}

/*
 * 寻找里目前时间最近的时间事件
 * 因为链表是乱序的，所以查找复杂度为 O（N）
 */
static evTimeEvent *evSearchNearestTimer(evEventLoop *eventLoop)
{
    evTimeEvent *te = NULL;
    evTimeEvent *nearest = NULL;
    int i;

    for (i=0; i<EV_TIMEEVENT_SIZE; i++)
    {
        if (!eventLoop->timeEvents[i].useFlag)
            continue;

        te = &eventLoop->timeEvents[i];
        if (!nearest || te->when_sec < nearest->when_sec ||
                (te->when_sec == nearest->when_sec &&
                 te->when_ms < nearest->when_ms))
            nearest = te;
    }

    return nearest;
}

static int evSearchNearestTime(evEventLoop *eventLoop, struct timeval *tvp)
{
    evTimeEvent *shortest = NULL;
    long now_sec, now_ms;
    long long ms;

    shortest = evSearchNearestTimer(eventLoop);
    if (shortest == NULL) return -1;

    evGetTime(&now_sec, &now_ms);

    ms = (shortest->when_sec - now_sec)*1000 +
            shortest->when_ms - now_ms;

    if (ms > 0)
    {
        tvp->tv_sec = ms/1000;
        tvp->tv_usec = (ms % 1000)*1000;
    }
    else
    {
        tvp->tv_sec = 0;
        tvp->tv_usec = 0;
    }

    return 0;
}

/*
 * 处理所有已到达的时间事件
 */
static int processTimeEvents(evEventLoop *eventLoop)
{
    int processed = 0,i;
    evTimeEvent *te;
    time_t now = time(NULL);

    /* 通过重置事件的运行时间，防止因时间倒流而造成的事件处理混乱 */
    if (now < eventLoop->lastTime)
    {
        for (i=0; i<EV_TIMEEVENT_SIZE; i++)
        {
            eventLoop->timeEvents[i].when_sec = 0;
        }
    }
    eventLoop->lastTime = now;

    for (i=0; i<EV_TIMEEVENT_SIZE; i++)
    {
        long now_sec, now_ms;
        long long id;

        if (!eventLoop->timeEvents[i].useFlag) continue;

        te = &eventLoop->timeEvents[i];

        evGetTime(&now_sec, &now_ms);

        if (now_sec > te->when_sec ||
            (now_sec == te->when_sec && now_ms >= te->when_ms))
        {
            int retval;

            id = te->id;
            retval = te->timeProc(eventLoop, id, te->clientData);
            processed++;

            if (retval != EV_NOMORE)
            {
                evAddMillisecondsToNow(retval,&te->when_sec,&te->when_ms);
            }
            else
            {
                evDeleteTimeEvent(eventLoop, id);
            }
        }
    }
    return processed;
}

/*
 * 处理文件事件
 */
static int processFileEvents(evEventLoop *eventLoop, evFiredEvent *fired)
{
    evFileEvent *fe = NULL;
    int rfired = 0;

    fe = &eventLoop->events[fired->fd];

    if (fe->mask & fired->mask & EV_READABLE)
    {
        /* rfired 确保读/写事件只能执行其中一个 */
        rfired = 1;
        fe->rfileProc(eventLoop,fired->fd,fe->clientData,fired->mask);
    }

    if (fe->mask & fired->mask & EV_WRITABLE)
    {
        if (!rfired || fe->wfileProc != fe->rfileProc)
            fe->wfileProc(eventLoop,fired->fd,fe->clientData,fired->mask);
    }

    return 1;
}

/*
 * 处理所有已到达的时间事件，以及所有已就绪的文件事件。
 * 如果不传入特殊 flags 的话，那么函数睡眠直到文件事件就绪，
 * 或者下个时间事件到达（如果有的话）。
 * 如果 flags 为 0 ，那么函数不作动作，直接返回。
 * 如果 flags 包含 EV_ALL_EVENTS ，所有类型的事件都会被处理。
 * 如果 flags 包含 EV_FILE_EVENTS ，那么处理文件事件。
 * 如果 flags 包含 EV_TIME_EVENTS ，那么处理时间事件。
 * 如果 flags 包含 EV_DONT_WAIT ，
 * 那么函数在处理完所有不许阻塞的事件之后，即刻返回。
 * 函数的返回值为已处理事件的数量
 */
int evProcessEvents(evEventLoop *eventLoop, int flags)
{
    int processed = 0, numevents;

    if (!(flags & EV_TIME_EVENTS) && !(flags & EV_FILE_EVENTS)) return 0;

    if (eventLoop->maxfd != -1 ||
        ((flags & EV_TIME_EVENTS) && !(flags & EV_DONT_WAIT)))
    {
        int j;
        struct timeval tv;
        struct timeval* tvp = NULL;

        if (flags & EV_TIME_EVENTS && !(flags & EV_DONT_WAIT))
        {
            if (evSearchNearestTime(eventLoop, &tv)>=0)
            {
                tvp = &tv;
            }
        }

        if (tvp == NULL)
        {
            if (flags & EV_DONT_WAIT)
            {
                tv.tv_sec = tv.tv_usec = 0;
                tvp = &tv;
            }
            else
            {
                tv.tv_sec = 0;
                tv.tv_usec = 100*1000;
                tvp = &tv;
            }
        }

        numevents = evApiPoll(eventLoop, tvp);
        for (j = 0; j < numevents; j++)
        {
            processed += processFileEvents(eventLoop, &eventLoop->fired[j]);
        }
    }

    /* Check time events */
    if (flags & EV_TIME_EVENTS)
        processed += processTimeEvents(eventLoop);

    return processed; /* return the number of processed file/time events */
}

/*
 * 事件处理器的主循环
 */
void evMain(evEventLoop *eventLoop)
{
    eventLoop->stop = 0;

    while (!eventLoop->stop)
    {
        if (eventLoop->beforesleep != NULL)
            eventLoop->beforesleep(eventLoop);

        evProcessEvents(eventLoop, EV_ALL_EVENTS);
    }
}

/*
 * 返回所使用的多路复用库的名称
 */
char *evGetApiName(void)
{
    return evApiName();
}

/*
 * 设置处理事件前需要被执行的函数
 */
void evSetBeforeSleepProc(evEventLoop *eventLoop, evBeforeSleepProc *beforesleep)
{
    eventLoop->beforesleep = beforesleep;
}
