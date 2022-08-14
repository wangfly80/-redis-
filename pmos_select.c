#include <string.h>

#ifdef WIN32
#undef FD_SETSIZE
#define FD_SETSIZE 1024
#include <winsock2.h>
#include <io.h>
#define EV_FD_TO_WIN32_HANDLE(fd) _get_osfhandle(fd)
#else
#include <sys/select.h>
#endif

typedef struct evApiState
{
    fd_set rfds, wfds;
    /* We need to have a copy of the fd sets as it's not safe to reuse
     * FD sets after select(). */
    fd_set _rfds, _wfds;
} evApiState;

static int evApiCreate(evEventLoop *eventLoop)
{
    evApiState *state = malloc(sizeof(evApiState));

    if (!state) return -1;
    FD_ZERO(&state->rfds);
    FD_ZERO(&state->wfds);
    eventLoop->apidata = state;
    return 0;
}

static void evApiFree(evEventLoop *eventLoop)
{
    free(eventLoop->apidata);
}

static int evApiAddEvent(evEventLoop *eventLoop, int fd, int mask)
{
    evApiState *state = eventLoop->apidata;
#ifndef WIN32
    if (mask & EV_READABLE) FD_SET(fd,&state->rfds);
    if (mask & EV_WRITABLE) FD_SET(fd,&state->wfds);
#else
    if (mask & EV_READABLE) FD_SET(EV_FD_TO_WIN32_HANDLE(fd),&state->rfds);
    if (mask & EV_WRITABLE) FD_SET(EV_FD_TO_WIN32_HANDLE(fd),&state->wfds);
#endif
    return 0;
}

static void evApiDelEvent(evEventLoop *eventLoop, int fd, int mask)
{
    evApiState *state = eventLoop->apidata;
#ifndef WIN32
    if (mask & EV_READABLE) FD_CLR(fd,&state->rfds);
    if (mask & EV_WRITABLE) FD_CLR(fd,&state->wfds);
#else
    if (mask & EV_READABLE) FD_CLR(EV_FD_TO_WIN32_HANDLE(fd),&state->rfds);
    if (mask & EV_WRITABLE) FD_CLR(EV_FD_TO_WIN32_HANDLE(fd),&state->wfds);
#endif
}

static int evApiPoll(evEventLoop *eventLoop, struct timeval *tvp)
{
    evApiState *state = eventLoop->apidata;
    int retval, j, numevents = 0;

    memcpy(&state->_rfds,&state->rfds,sizeof(fd_set));
    memcpy(&state->_wfds,&state->wfds,sizeof(fd_set));

    retval = select(eventLoop->maxfd+1,
                &state->_rfds,&state->_wfds,NULL,tvp);
    if (retval > 0) {
        for (j = 0; j <= eventLoop->maxfd; j++) {
            int mask = 0;
            evFileEvent *fe = &eventLoop->events[j];

            if (fe->mask == EV_NONE) continue;
#ifndef WIN32
            if (fe->mask & EV_READABLE && FD_ISSET(j,&state->_rfds))
                mask |= EV_READABLE;
            if (fe->mask & EV_WRITABLE && FD_ISSET(j,&state->_wfds))
                mask |= EV_WRITABLE;
#else
            if (fe->mask & EV_READABLE && FD_ISSET(EV_FD_TO_WIN32_HANDLE(j),&state->_rfds))
                mask |= EV_READABLE;
            if (fe->mask & EV_WRITABLE && FD_ISSET(EV_FD_TO_WIN32_HANDLE(j),&state->_wfds))
                mask |= EV_WRITABLE;
#endif
            eventLoop->fired[numevents].fd = j;
            eventLoop->fired[numevents].mask = mask;
            numevents++;
        }
    }
    return numevents;
}

static char *evApiName(void)
{
    return "select";
}
