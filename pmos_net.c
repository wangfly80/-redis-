#include "pmos_net.h"

static void netSetError(char *err, const char *fmt, ...)
{
    va_list ap;

    if (!err) return;
    va_start(ap, fmt);
    vsnprintf(err, NET_ERR_LEN, fmt, ap);
    va_end(ap);
}

#ifndef WIN32
int netSetBlock(char *err, int fd, int non_block)
{
    int flags;

    if ((flags = fcntl(fd, F_GETFL)) == -1)
    {
        netSetError(err, "fcntl(F_GETFL): %d", errno);
        return NET_ERR;
    }

    if (non_block)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;

    if (fcntl(fd, F_SETFL, flags) == -1)
    {
        netSetError(err, "fcntl(F_SETFL,O_NONBLOCK): %d", errno);
        return NET_ERR;
    }

    return NET_OK;
}

int netNonBlock(char *err, int fd)
{
    return netSetBlock(err,fd,1);
}

int netBlock(char *err, int fd)
{
    return netSetBlock(err,fd,0);
}

/* Set TCP keep alive option to detect dead peers. The interval option
 * is only used for Linux as we are using Linux-specific APIs to set
 * the probe send time, interval, and count.
 *
 * 修改 TCP 连接的 keep alive 选项
 */
int netKeepAlive(char *err, int fd, int interval)
{
    int val = 1;

    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val)) == -1)
    {
        netSetError(err, "setsockopt SO_KEEPALIVE: %s", strerror(errno));
        return NET_ERR;
    }

    /* Default settings are more or less garbage, with the keepalive time
     * set to 7200 by default on Linux. Modify settings to make the feature
     * actually useful. */

    /* Send first probe after interval. */
    val = interval;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &val, sizeof(val)) < 0) {
        netSetError(err, "setsockopt TCP_KEEPIDLE: %s\n", strerror(errno));
        return NET_ERR;
    }

    /* Send next probes after the specified interval. Note that we set the
     * delay as interval / 3, as we send three probes before detecting
     * an error (see the next setsockopt call). */
    val = interval/3;
    if (val == 0) val = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &val, sizeof(val)) < 0) {
        netSetError(err, "setsockopt TCP_KEEPINTVL: %s\n", strerror(errno));
        return NET_ERR;
    }

    /* Consider the socket in error state after three we send three ACK
     * probes without getting a reply. */
    val = 3;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &val, sizeof(val)) < 0) {
        netSetError(err, "setsockopt TCP_KEEPCNT: %s\n", strerror(errno));
        return NET_ERR;
    }

    return NET_OK;
}

/*
 * 打开或关闭 Nagle 算法
 */
static int netSetTcpNoDelay(char *err, int fd, int val)
{
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const void *)&val, sizeof(val)) == -1)
    {
        netSetError(err, "setsockopt TCP_NODELAY: %d", errno);
        return NET_ERR;
    }
    return NET_OK;
}

/*
 * 禁用 Nagle 算法
 */
int netEnableTcpNoDelay(char *err, int fd)
{
    return netSetTcpNoDelay(err, fd, 1);
}

/*
 * 启用 Nagle 算法
 */
int netDisableTcpNoDelay(char *err, int fd)
{
    return netSetTcpNoDelay(err, fd, 0);
}

/*
 * 设置 socket 的最大发送 buffer 字节数
 */
int netSetSendBuffer(char *err, int fd, int buffsize)
{
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (const void *)&buffsize, sizeof(buffsize)) == -1)
    {
        netSetError(err, "setsockopt SO_SNDBUF: %d", errno);
        return NET_ERR;
    }
    return NET_OK;
}

/*
 * 开启 TCP 的 keep alive 选项
 */
int netTcpKeepAlive(char *err, int fd)
{
    int yes = 1;

    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (const void *)&yes, sizeof(yes)) == -1)
    {
        netSetError(err, "setsockopt SO_KEEPALIVE: %d", errno);
        return NET_ERR;
    }
    return NET_OK;
}

/* Set the socket send timeout (SO_SNDTIMEO socket option) to the specified
 * number of milliseconds, or disable it if the 'ms' argument is zero. */
int netSendTimeout(char *err, int fd, long long ms)
{
    struct timeval tv;

    tv.tv_sec = ms/1000;
    tv.tv_usec = (ms%1000)*1000;

    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const void *)&tv, sizeof(tv)) == -1)
    {
        netSetError(err, "setsockopt SO_SNDTIMEO: %d", errno);
        return NET_ERR;
    }
    return NET_OK;
}

/*
 * 设置地址为可重用
 */
static int netSetReuseAddr(char *err, int fd)
{
    int yes = 1;

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const void *)&yes, sizeof(yes)) == -1)
    {
        netSetError(err, "setsockopt SO_REUSEADDR: %d", errno);
        return NET_ERR;
    }
    return NET_OK;
}

int netCloseSocket(int fd)
{
    return close(fd);
}

static int BindSourceAddr(char *err, int fd, char * source_addr)
{
    int rv, bound = 0;
    struct addrinfo hints, *bservinfo, *b;

    if ((rv = getaddrinfo(source_addr, NULL, &hints, &bservinfo)) != 0)
    {
        netSetError(err, "%s", gai_strerror(rv));
        return NET_ERR;
    }

    for (b = bservinfo; b != NULL; b = b->ai_next)
    {
        if (bind(fd,b->ai_addr,b->ai_addrlen) != -1)
        {
            bound = 1;
            break;
        }
    }
    freeaddrinfo(bservinfo);

    if (!bound)
    {
        netSetError(err, "bind: %d", errno);
        return NET_ERR;
    }

    return NET_OK;
}

#define NET_CONNECT_NONE 0
#define NET_CONNECT_NONBLOCK 1
static int netTcpGenericConnect(char *err, char *addr, int port,
                                 char *source_addr, int flags)
{
    int s = NET_ERR, rv, resultflag=-1;
    char portstr[6];  /* strlen("65535") + 1; */
    struct addrinfo hints, *servinfo, *p;

    snprintf(portstr,sizeof(portstr),"%d",port);
    memset(&hints,0,sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(addr,portstr,&hints,&servinfo)) != 0)
    {
        netSetError(err, "%s", gai_strerror(rv));
        return NET_ERR;
    }

    for (p = servinfo; p != NULL; p = p->ai_next)
    {
        if ((s = socket(p->ai_family,p->ai_socktype,p->ai_protocol)) == -1)
            continue;

        if (netSetReuseAddr(err,s) == NET_ERR)
        {
            resultflag = -1;
            break;
        }

        if (flags & NET_CONNECT_NONBLOCK && netNonBlock(err,s) != NET_OK)
        {
            resultflag = -1;
            break;
        }

        if (source_addr)
        {
            if (BindSourceAddr(err, s, source_addr) != 0)
            {
                resultflag = -1;
                break;
            }
        }

        if (connect(s,p->ai_addr,p->ai_addrlen) == -1)
        {
            if (errno == EINPROGRESS && flags & NET_CONNECT_NONBLOCK)
            {
                resultflag = 0;
                break;
            }
            netCloseSocket(s);
            s = NET_ERR;
            continue;
        }

        resultflag = 0;
        break;
    }

    if (resultflag == -1 && s != NET_ERR)
    {
        netCloseSocket(s);
        s = NET_ERR;
    }

    freeaddrinfo(servinfo);
    return s;
}

/*
 * 创建阻塞 TCP 连接
 */
int netTcpConnect(char *err, char *addr, int port)
{
    return netTcpGenericConnect(err,addr,port,NULL,NET_CONNECT_NONE);
}

/*
 * 创建非阻塞 TCP 连接
 */
int netTcpNonBlockConnect(char *err, char *addr, int port)
{
    return netTcpGenericConnect(err,addr,port,NULL,NET_CONNECT_NONBLOCK);
}

int netTcpNonBlockBindConnect(char *err, char *addr, int port, char *source_addr)
{
    return netTcpGenericConnect(err,addr,port,source_addr,NET_CONNECT_NONBLOCK);
}

/*
 * 绑定并创建监听套接字
 */
static int netListen(char *err, int s, struct sockaddr *sa, socklen_t len, int backlog)
{
    if (bind(s,sa,len) == -1)
    {
        netSetError(err, "bind: %d", errno);
        netCloseSocket(s);
        return NET_ERR;
    }

    if (listen(s, backlog) == -1)
    {
        netSetError(err, "listen: %d", errno);
        netCloseSocket(s);
        return NET_ERR;
    }
    return NET_OK;
}

static int netV6Only(char *err, int s)
{
    int yes = 1;

    if (setsockopt(s,IPPROTO_IPV6,IPV6_V6ONLY,(const void *)&yes,sizeof(yes)) == -1)
    {
        netSetError(err, "setsockopt: %d", errno);
        netCloseSocket(s);
        return NET_ERR;
    }
    return NET_OK;
}

static int _netTcpServer(char *err, int port, char *bindaddr, int af, int backlog)
{
    int s, rv, resultflag=-1;
    char _port[6];  /* strlen("65535") */
    struct addrinfo hints, *servinfo, *p;

    snprintf(_port,6,"%d",port);
    memset(&hints,0,sizeof(hints));
    hints.ai_family = af;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;    /* No effect if bindaddr != NULL */

    if ((rv = getaddrinfo(bindaddr,_port,&hints,&servinfo)) != 0)
    {
        netSetError(err, "%s", gai_strerror(rv));
        return NET_ERR;
    }

    for (p = servinfo; p != NULL; p = p->ai_next)
    {
        if ((s = socket(p->ai_family,p->ai_socktype,p->ai_protocol)) == -1)
            continue;

        if (af == AF_INET6 && netV6Only(err,s) == NET_ERR)
            break;

        if (netSetReuseAddr(err,s) == NET_ERR)
            break;

        if (netListen(err,s,p->ai_addr,p->ai_addrlen,backlog) == NET_ERR)
            break;

        resultflag = 0;
        break;
    }

    if (p == NULL)
    {
        netSetError(err, "unable to bind socket");
        resultflag = -1;
    }

    if (resultflag == -1)
        s = NET_ERR;

    freeaddrinfo(servinfo);
    return s;
}

int netTcpServer(char *err, int port, char *bindaddr, int backlog)
{
    return _netTcpServer(err, port, bindaddr, AF_INET, backlog);
}

int netTcp6Server(char *err, int port, char *bindaddr, int backlog)
{
    return _netTcpServer(err, port, bindaddr, AF_INET6, backlog);
}

static int netGenericAccept(char *err, int s, struct sockaddr *sa, socklen_t *len)
{
    int fd;
    while(1)
    {
        fd = accept(s,sa,len);
        if (fd == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }
            netSetError(err, "accept: %d", errno);
            return NET_ERR;
        }
        break;
    }
    return fd;
}

/*
 * TCP 连接 accept 函数
 */
int netTcpAccept(char *err, int s, char *ip, size_t ip_len, int *port)
{
    int fd;
    struct sockaddr_storage sa;
    socklen_t salen = sizeof(sa);
    if ((fd = netGenericAccept(err,s,(struct sockaddr*)&sa,&salen)) == -1)
        return NET_ERR;

    if (sa.ss_family == AF_INET)
    {
        struct sockaddr_in *s = (struct sockaddr_in *)&sa;
        if (ip)
        {
            inet_ntop(AF_INET,(void*)&(s->sin_addr),ip,ip_len);
        }
        if (port) *port = ntohs(s->sin_port);
    }
    else
    {
        struct sockaddr_in6 *s = (struct sockaddr_in6 *)&sa;
        if (ip)
        {
            inet_ntop(AF_INET6,(void*)&(s->sin6_addr),ip,ip_len);
        }
        if (port) *port = ntohs(s->sin6_port);
    }
    return fd;
}

int netListenToPort(char** bindaddr, int bindaddr_count, int port, int backlog,
                 int *fds, int *count, char *err)
{
    int j;

    /* Force binding of 0.0.0.0 if no bind address is specified, always
     * entering the loop if j == 0. */
    if (bindaddr_count == 0) bindaddr[0] = NULL;
    for (j = 0; j < bindaddr_count || j == 0; j++)
    {
        if (bindaddr[j] == NULL)
        {
            /* Bind * for both IPv6 and IPv4, we enter here only if
             * server.bindaddr_count == 0. */
            fds[*count] = netTcp6Server(err,port,NULL,backlog);
            if (fds[*count] != NET_ERR)
            {
                netNonBlock(NULL,fds[*count]);
                (*count)++;
            }
            fds[*count] = netTcpServer(err,port,NULL,backlog);
            if (fds[*count] != NET_ERR)
            {
                netNonBlock(NULL,fds[*count]);
                (*count)++;
            }
            /* Exit the loop if we were able to bind * on IPv4 or IPv6,
             * otherwise fds[*count] will be NET_ERR and we'll print an
             * error and return to the caller with an error. */
            if (*count) break;
        }
        else if (strchr(bindaddr[j],':'))
        {
            /* Bind IPv6 address. */
            fds[*count] = netTcp6Server(err,port,bindaddr[j],backlog);
        }
        else
        {
            /* Bind IPv4 address. */
            fds[*count] = netTcpServer(err,port,bindaddr[j],backlog);
        }

        if (fds[*count] == NET_ERR)
        {
            netSetError(err, "Creating Server TCP listening socket %s:%d: %s",
                        bindaddr[j] ? bindaddr[j] : "*", port, err);
            return NET_ERR;
        }
        netNonBlock(NULL,fds[*count]);
        (*count)++;
    }

    return NET_OK;
}

int netGetSockError(int fd)
{
    int sockerr = 0;
    socklen_t errlen = sizeof(sockerr);

    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (void *)&sockerr, &errlen) == -1)
    {
        sockerr = errno;
    }

    return sockerr;
}

#else
int netSetBlock(char *err, int socket, int non_block)
{
    unsigned long flags = (unsigned long)non_block;

    if (ioctlsocket(socket, FIONBIO, &flags) != 0)
    {
        netSetError(err, "ioctlsocket: %d", errno);
        return NET_ERR;
    }

    return NET_OK;
}

int netNonBlock(char *err, int fd)
{
    int socket = EV_FD_TO_WIN32_HANDLE(fd);
    return netSetBlock(err,socket,1);
}

int netBlock(char *err, int fd)
{
    int socket = EV_FD_TO_WIN32_HANDLE(fd);
    return netSetBlock(err,socket,0);
}

int netKeepAlive(char *err, int fd, int interval)
{
    int result = SOCKET_ERROR;
    int socket = EV_FD_TO_WIN32_HANDLE(fd);
    BOOL bKeepAlive = TRUE;
    struct tcp_keepalive alive_in = {0};
    struct  tcp_keepalive alive_out = {0};
    unsigned long bytes_return = 0;

    result = setsockopt(socket, SOL_SOCKET, SO_KEEPALIVE,(const char*)&bKeepAlive, sizeof(bKeepAlive));
    if (result == SOCKET_ERROR)
    {
        netSetError(err, "setsockopt failed: %d/n", errno);
        return NET_ERR;
    }

    alive_in.keepalivetime      = 1000*interval;
    alive_in.keepaliveinterval  = 1000*interval/3;
    alive_in.onoff              = TRUE;

    result = WSAIoctl(socket, SIO_KEEPALIVE_VALS, &alive_in, sizeof(alive_in), &alive_out, sizeof(alive_out), &bytes_return, NULL, NULL);
    if (result == SOCKET_ERROR)
    {
        netSetError(err, "WSAIoctl failed: %d/n", errno);
        return NET_ERR;
    }

    return NET_OK;
}

/*
 * 打开或关闭 Nagle 算法
 */
static int netSetTcpNoDelay(char *err, int socket, int val)
{
    if (setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (const void *)&val, sizeof(val)) == -1)
    {
        netSetError(err, "setsockopt TCP_NODELAY: %d", errno);
        return NET_ERR;
    }
    return NET_OK;
}

/*
 * 禁用 Nagle 算法
 */
int netEnableTcpNoDelay(char *err, int fd)
{
    int socket = EV_FD_TO_WIN32_HANDLE(fd);
    return netSetTcpNoDelay(err, socket, 1);
}

/*
 * 启用 Nagle 算法
 */
int netDisableTcpNoDelay(char *err, int fd)
{
    int socket = EV_FD_TO_WIN32_HANDLE(fd);
    return netSetTcpNoDelay(err, socket, 0);
}

/*
 * 设置 socket 的最大发送 buffer 字节数
 */
int netSetSendBuffer(char *err, int fd, int buffsize)
{
    int socket = EV_FD_TO_WIN32_HANDLE(fd);

    if (setsockopt(socket, SOL_SOCKET, SO_SNDBUF, (const void *)&buffsize, sizeof(buffsize)) == -1)
    {
        netSetError(err, "setsockopt SO_SNDBUF: %d", errno);
        return NET_ERR;
    }
    return NET_OK;
}

/*
 * 开启 TCP 的 keep alive 选项
 */
int netTcpKeepAlive(char *err, int fd)
{
    int yes = 1;
    int socket = EV_FD_TO_WIN32_HANDLE(fd);

    if (setsockopt(socket, SOL_SOCKET, SO_KEEPALIVE, (const void *)&yes, sizeof(yes)) == -1)
    {
        netSetError(err, "setsockopt SO_KEEPALIVE: %d", errno);
        return NET_ERR;
    }
    return NET_OK;
}

/* Set the socket send timeout (SO_SNDTIMEO socket option) to the specified
 * number of milliseconds, or disable it if the 'ms' argument is zero. */
int netSendTimeout(char *err, int fd, long long ms)
{
    int socket = EV_FD_TO_WIN32_HANDLE(fd);
    struct timeval tv;

    tv.tv_sec = ms/1000;
    tv.tv_usec = (ms%1000)*1000;

    if (setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, (const void *)&tv, sizeof(tv)) == -1)
    {
        netSetError(err, "setsockopt SO_SNDTIMEO: %d", errno);
        return NET_ERR;
    }
    return NET_OK;
}

int netCloseSocket(int fd)
{
    return _close(fd);
}

/*
 * 设置地址为可重用
 */
static int netSetReuseAddr(char *err, int socket)
{
    int yes = 1;

    if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, (const void *)&yes, sizeof(yes)) == -1)
    {
        netSetError(err, "setsockopt SO_REUSEADDR: %d", errno);
        return NET_ERR;
    }
    return NET_OK;
}

static int BindSourceAddr(char *err, int socket, char * source_addr)
{
    int rv, bound = 0;
    struct addrinfo hints, *bservinfo, *b;

    if ((rv = getaddrinfo(source_addr, NULL, &hints, &bservinfo)) != 0)
    {
        netSetError(err, "%s", gai_strerror(rv));
        return NET_ERR;
    }

    for (b = bservinfo; b != NULL; b = b->ai_next)
    {
        if (bind(socket,b->ai_addr,b->ai_addrlen) != -1)
        {
            bound = 1;
            break;
        }
    }
    freeaddrinfo(bservinfo);

    if (!bound)
    {
        netSetError(err, "bind: %d", errno);
        return NET_ERR;
    }

    return NET_OK;
}

#define NET_CONNECT_NONE 0
#define NET_CONNECT_NONBLOCK 1
static int netTcpGenericConnect(char *err, char *addr, int port,
                                 char *source_addr, int flags)
{
    int s = NET_ERR, rv, resultflag=-1;
    char portstr[6];  /* strlen("65535") + 1; */
    struct addrinfo hints, *servinfo, *p;

    snprintf(portstr,sizeof(portstr),"%d",port);
    memset(&hints,0,sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(addr,portstr,&hints,&servinfo)) != 0)
    {
        netSetError(err, "%s", gai_strerror(rv));
        return NET_ERR;
    }

    for (p = servinfo; p != NULL; p = p->ai_next)
    {
        if ((s = socket(p->ai_family,p->ai_socktype,p->ai_protocol)) == -1)
            continue;

        if (netSetReuseAddr(err,s) == NET_ERR)
        {
            resultflag = -1;
            break;
        }

        if (flags & NET_CONNECT_NONBLOCK && netSetBlock(err,s,1) != NET_OK)
        {
            resultflag = -1;
            break;
        }

        if (source_addr)
        {
            if (BindSourceAddr(err, s, source_addr) != 0)
            {
                resultflag = -1;
                break;
            }
        }

        if (connect(s,p->ai_addr,p->ai_addrlen) == -1)
        {
            if (errno == EINPROGRESS && flags & NET_CONNECT_NONBLOCK)
            {
                resultflag = 0;
                break;
            }
            closesocket(s);
            s = NET_ERR;
            continue;
        }

        resultflag = 0;
        break;
    }

    if (resultflag == -1 && s != NET_ERR)
    {
        closesocket(s);
        s = NET_ERR;
    }

    freeaddrinfo(servinfo);
    return s;
}

/*
 * 创建阻塞 TCP 连接
 */
int netTcpConnect(char *err, char *addr, int port)
{
    int socket;

    socket = netTcpGenericConnect(err,addr,port,NULL,NET_CONNECT_NONE);
    if (socket != NET_ERR)
    {        
        return EV_WIN32_HANDLE_TO_FD(socket);
    }
    return NET_ERR;
}

/*
 * 创建非阻塞 TCP 连接
 */
int netTcpNonBlockConnect(char *err, char *addr, int port)
{
    int socket;

    socket = netTcpGenericConnect(err,addr,port,NULL,NET_CONNECT_NONBLOCK);
    if (socket != NET_ERR)
    {
        return EV_WIN32_HANDLE_TO_FD(socket);
    }
    return NET_ERR;
}

int netTcpNonBlockBindConnect(char *err, char *addr, int port, char *source_addr)
{
    int socket;

    socket = netTcpGenericConnect(err,addr,port,source_addr,NET_CONNECT_NONBLOCK);
    if (socket != NET_ERR)
    {
        return EV_WIN32_HANDLE_TO_FD(socket);
    }
    return NET_ERR;
}

/*
 * 绑定并创建监听套接字
 */
static int netListen(char *err, int s, struct sockaddr *sa, socklen_t len, int backlog)
{
    if (bind(s,sa,len) == -1)
    {
        netSetError(err, "bind: %d", errno);
        closesocket(s);
        return NET_ERR;
    }

    if (listen(s, backlog) == -1)
    {
        netSetError(err, "listen: %d", errno);
        closesocket(s);
        return NET_ERR;
    }
    return NET_OK;
}

static int netV6Only(char *err, int s)
{
    int yes = 1;

    if (setsockopt(s,IPPROTO_IPV6,IPV6_V6ONLY,(const void *)&yes,sizeof(yes)) == -1)
    {
        netSetError(err, "setsockopt: %d", errno);
        closesocket(s);
        return NET_ERR;
    }
    return NET_OK;
}

static int _netTcpServer(char *err, int port, char *bindaddr, int af, int backlog)
{
    int s, rv, resultflag=-1;
    char _port[6];  /* strlen("65535") */
    struct addrinfo hints, *servinfo, *p;

    snprintf(_port,6,"%d",port);
    memset(&hints,0,sizeof(hints));
    hints.ai_family = af;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;    /* No effect if bindaddr != NULL */

    if ((rv = getaddrinfo(bindaddr,_port,&hints,&servinfo)) != 0)
    {
        netSetError(err, "%s", gai_strerror(rv));
        return NET_ERR;
    }

    for (p = servinfo; p != NULL; p = p->ai_next)
    {
        if ((s = socket(p->ai_family,p->ai_socktype,p->ai_protocol)) == -1)
            continue;

        if (af == AF_INET6 && netV6Only(err,s) == NET_ERR)
            break;

        if (netSetReuseAddr(err,s) == NET_ERR)
            break;

        if (netListen(err,s,p->ai_addr,p->ai_addrlen,backlog) == NET_ERR)
            break;

        resultflag = 0;
        break;
    }

    if (p == NULL)
    {
        netSetError(err, "unable to bind socket");
        resultflag = -1;
    }

    if (resultflag == -1)
        s = NET_ERR;

    freeaddrinfo(servinfo);
    return s;
}

static int netTcpServer(char *err, int port, char *bindaddr, int backlog)
{
    return _netTcpServer(err, port, bindaddr, AF_INET, backlog);
}

static int netTcp6Server(char *err, int port, char *bindaddr, int backlog)
{
    return _netTcpServer(err, port, bindaddr, AF_INET6, backlog);
}

static int netGenericAccept(char *err, int s, struct sockaddr *sa, socklen_t *len)
{
    int fd;
    int socket = EV_FD_TO_WIN32_HANDLE(s);

    while(1)
    {
        fd = accept(socket,sa,len);
        if (fd == -1)
        {
            netSetError(err, "accept: %d", errno);
            return NET_ERR;
        }
        break;
    }

    return EV_WIN32_HANDLE_TO_FD(fd);
}

/*
 * TCP 连接 accept 函数
 */
int netTcpAccept(char *err, int sfd, char *ip, size_t ip_len, int *port)
{
    int fd;
    struct sockaddr_storage sa;
    socklen_t salen = sizeof(sa);

    if ((fd = netGenericAccept(err,sfd,(struct sockaddr*)&sa,&salen)) == -1)
        return NET_ERR;

    if (sa.ss_family == AF_INET)
    {
        struct sockaddr_in *s = (struct sockaddr_in *)&sa;
        if (ip)
        {
            sprintf(ip,"%s",inet_ntoa(s->sin_addr));
        }
        if (port) *port = ntohs(s->sin_port);
    }
    else
    {
        struct sockaddr_in6 *s = (struct sockaddr_in6 *)&sa;
        if (ip)
        {
            WSAAddressToString((LPSOCKADDR)s,sizeof(struct sockaddr),NULL,ip,(LPDWORD)&ip_len);
        }
        if (port) *port = ntohs(s->sin6_port);
    }
    return fd;
}

int netListenToPort(char** bindaddr, int bindaddr_count, int port, int backlog,
                 int *fds, int *count, char *err)
{
    int j,socket;

    /* Force binding of 0.0.0.0 if no bind address is specified, always
     * entering the loop if j == 0. */
    if (bindaddr_count == 0) bindaddr[0] = NULL;
    for (j = 0; j < bindaddr_count || j == 0; j++)
    {
        if (bindaddr[j] == NULL)
        {
            /* Bind * for both IPv6 and IPv4, we enter here only if
             * server.bindaddr_count == 0. */
            socket = netTcp6Server(err,port,NULL,backlog);
            if (socket != NET_ERR)
            {
                fds[*count] = EV_WIN32_HANDLE_TO_FD(socket);
                netNonBlock(NULL,fds[*count]);
                (*count)++;
            }
            socket = netTcpServer(err,port,NULL,backlog);
            if (socket != NET_ERR)
            {
                fds[*count] = EV_WIN32_HANDLE_TO_FD(socket);
                netNonBlock(NULL,fds[*count]);
                (*count)++;
            }
            /* Exit the loop if we were able to bind * on IPv4 or IPv6,
             * otherwise fds[*count] will be NET_ERR and we'll print an
             * error and return to the caller with an error. */
            if (*count) break;
        }
        else if (strchr(bindaddr[j],':'))
        {
            /* Bind IPv6 address. */
            socket = netTcp6Server(err,port,bindaddr[j],backlog);
        }
        else
        {
            /* Bind IPv4 address. */
            socket = netTcpServer(err,port,bindaddr[j],backlog);
        }

        if (socket == NET_ERR)
        {
            netSetError(err, "Creating Server TCP listening socket %s:%d: %s",
                        bindaddr[j] ? bindaddr[j] : "*", port, err);
            return NET_ERR;
        }
        fds[*count] = EV_WIN32_HANDLE_TO_FD(socket);
        netNonBlock(NULL,fds[*count]);
        (*count)++;
    }

    return NET_OK;
}

int netGetSockError(int fd)
{
    int sockerr = 0;
    int socket = EV_FD_TO_WIN32_HANDLE(fd);
    socklen_t errlen = sizeof(sockerr);

    if (getsockopt(socket, SOL_SOCKET, SO_ERROR, (void *)&sockerr, &errlen) == -1)
    {
        sockerr = errno;
    }

    return sockerr;
}


#endif
