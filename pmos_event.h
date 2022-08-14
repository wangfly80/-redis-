#ifndef _PMOS_EVENT_H_
#define _PMOS_EVENT_H_

#include <sys/time.h>

/*
 * 事件执行状态
 */
#define EV_OK          0
#define EV_ERROR       -1

/*
 * 文件事件状态
 */
#define EV_NONE        0
#define EV_READABLE    1
#define EV_WRITABLE    2

/*
 * 时间处理器的执行 flags
 */
#define EV_FILE_EVENTS 1
#define EV_TIME_EVENTS 2
#define EV_ALL_EVENTS (EV_FILE_EVENTS|EV_TIME_EVENTS)
#define EV_DONT_WAIT   4

/*
 * 决定时间事件是否要持续执行的 flag
 */
#define EV_NOMORE     -1

/* Macros */
#define EV_NOTUSED(V) ((void) V)

/*
 * 时间事件最大个数
 */
#define EV_TIMEEVENT_SIZE  10

/*
 * 事件处理器状态
 */
struct evEventLoop;

/* Types and data structures
 *
 * 事件接口
 */
typedef void evFileProc(struct evEventLoop *eventLoop, int fd, void *clientData, int mask);
typedef int  evTimeProc(struct evEventLoop *eventLoop, long long id, void *clientData);
typedef void evEventFinalizerProc(struct evEventLoop *eventLoop, void *clientData);
typedef void evBeforeSleepProc(struct evEventLoop *eventLoop);

/* File event structure
 *
 * 文件事件结构
 */
typedef struct evFileEvent {
    int                    mask; /* one of EV_(READABLE|WRITABLE) */
    evFileProc             *rfileProc;
    evFileProc             *wfileProc;
    void                   *clientData;
} evFileEvent;

/* Time event structure
 *
 * 时间事件结构
 */
typedef struct evTimeEvent {
    int                    useFlag;
    long long              id;       /* time event identifier. */
    long                   when_sec; /* seconds */
    long                   when_ms;  /* milliseconds */
    evTimeProc             *timeProc;
    evEventFinalizerProc   *finalizerProc;
    void                   *clientData;
} evTimeEvent;

/* A fired event
 *
 * 已就绪事件
 */
typedef struct evFiredEvent {
    int                    fd;
    int                    mask;
} evFiredEvent;

/* State of an event based program
 *
 * 事件处理器的状态
 */
typedef struct evEventLoop {
    int                    maxfd;   /* highest file descriptor currently registered */
    int                    setsize; /* max number of file descriptors tracked */
    long long              timeEventNextId;
    time_t                 lastTime;     /* Used to detect system clock skew */
    evFileEvent            *events; /* Registered events */
    evFiredEvent           *fired; /* Fired events */
    evTimeEvent            *timeEvents;
    int                    stop;
    void                   *apidata; /* This is used for polling API specific data */
    evBeforeSleepProc      *beforesleep;
} evEventLoop;

/* Prototypes */
evEventLoop *evCreateEventLoop(int setsize);
void        evDeleteEventLoop(evEventLoop *eventLoop);
void        evStop(evEventLoop *eventLoop);
int         evCreateFileEvent(evEventLoop *eventLoop, int fd, int mask,
                              evFileProc *proc, void *clientData);
void        evDeleteFileEvent(evEventLoop *eventLoop, int fd, int mask);
int         evGetFileEvents(evEventLoop *eventLoop, int fd);
long long   evCreateTimeEvent(evEventLoop *eventLoop, long long milliseconds,
                              evTimeProc *proc, void *clientData,
                              evEventFinalizerProc *finalizerProc);
int         evDeleteTimeEvent(evEventLoop *eventLoop, long long id);
int         evProcessEvents(evEventLoop *eventLoop, int flags);
void        evMain(evEventLoop *eventLoop);
char        *evGetApiName(void);
void        evSetBeforeSleepProc(evEventLoop *eventLoop, evBeforeSleepProc *beforesleep);
int         evGetSetSize(evEventLoop *eventLoop);

#endif
