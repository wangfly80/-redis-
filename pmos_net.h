#ifndef _PMOS_NET_H_
#define _PMOS_NET_H_

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#ifdef WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#endif


#define NET_OK         0
#define NET_ERR        -1
#define NET_ERR_LEN    256

/* Flags used with certain functions. */
#define NET_NONE 0
#define NET_IP_ONLY (1<<0)

#ifdef WIN32
#define EV_FD_TO_WIN32_HANDLE(fd)     _get_osfhandle(fd)
#define EV_WIN32_HANDLE_TO_FD(handle) _open_osfhandle(handle, 0)

#define netRead(fd,buf,len)     recv((EV_FD_TO_WIN32_HANDLE(fd)),(buf),(len),0)
#define netWrite(fd,buf,len)    send((EV_FD_TO_WIN32_HANDLE(fd)),(buf),(len),0)

#undef  errno
#define errno            WSAGetLastError()
#undef  EAGAIN
#define EAGAIN           WSAEWOULDBLOCK
#undef  EINPROGRESS
#define EINPROGRESS      WSAEWOULDBLOCK
#undef  EWOULDBLOCK
#define EWOULDBLOCK      WSAEWOULDBLOCK
#else
#define netRead(fd,buf,len)     read((fd),(buf),(len))
#define netWrite(fd,buf,len)    write((fd),(buf),(len))
#endif

int netTcpConnect(char *err, char *addr, int port);
int netTcpNonBlockConnect(char *err, char *addr, int port);
int netTcpNonBlockBindConnect(char *err, char *addr, int port, char *source_addr);
int netCloseSocket(int fd);
int netTcpAccept(char *err, int sfd, char *ip, size_t ip_len, int *port);
int netNonBlock(char *err, int fd);
int netBlock(char *err, int fd);
int netEnableTcpNoDelay(char *err, int fd);
int netDisableTcpNoDelay(char *err, int fd);
int netTcpKeepAlive(char *err, int fd);
int netSendTimeout(char *err, int fd, long long ms);
int netKeepAlive(char *err, int fd, int interval);
int netListenToPort(char** bindaddr, int bindaddr_count, int port, int backlog,
                 int *fds, int *count, char *err);
int netGetSockError(int fd);

#endif
