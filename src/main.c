/*
 * main.c - layer-4 proxy main module
 *
 * Copyright (c) 2015 Yang Li. All rights reserved.
 *
 * This program may be distributed according to the terms of the GNU
 * General Public License, version 3 or (at your option) any later version.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <errno.h>
#include <syslog.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <ev.h>

#include "utils.h"
#include "daemon.h"
#include "proxy.h"
#include "backends/backend.h"
#include "backends/redirect.h"

static int setnonblocking(int);
static int open_bind_socket(const char *addr, const char *port);

static void accept_callback(EV_P_ ev_io *watcher, int revents);

int
main(int argc, char *argv[]) {
    int opt;
    int detach = 0;
    char *host = NULL; 
    char *port = "1080";
    char *pidfile = "/var/run/l4proxy/pidfile";

    while((opt = getopt(argc, argv, "l:p:dP:")) != -1) {
        switch(opt) {
            case 'l':
                host = strdup(optarg);
                break;
            case 'p':
                port = strdup(optarg);
                break;
            case 'd':
                detach = 1;
                break;
            case 'P':
                pidfile = strdup(optarg);
                break;
            default:
                fprintf(stderr,
                        "Usage: %s [-d] [-l LISTEN_ADDR] [-p LISTENT_PORT] [-P pidfile]\n",
                        argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if(detach)
        daemonize();

    openlog("l4proxy", LOG_PID|LOG_PERROR, LOG_DAEMON);
    syslog(LOG_NOTICE, "l4proxy started");

    set_signal_handler(SIGPIPE, SIG_IGN);

    if(pidfile){
        int pidfd;
        struct flock pidfl;
        char buf[64];
        pidfl.l_type = F_WRLCK;
        pidfl.l_whence = SEEK_SET;
        pidfl.l_start = 0;
        pidfl.l_len = 0;
        if((pidfd = open(pidfile, O_RDWR|O_CREAT, 0600)) < 0){
            syslog(LOG_CRIT, "Couldn't open pidfile %s", pidfile);
            exit(EXIT_FAILURE);
        }
        if(fcntl(pidfd, F_SETLK, &pidfl) < 0){
            syslog(LOG_CRIT, "Couldn't lock pidfile %s", pidfile);
            exit(EXIT_FAILURE);
        }
        ftruncate(pidfd, 0);
        snprintf(buf, sizeof(buf), "%lu\n", (unsigned long) getpid());
        write(pidfd, buf, strlen(buf));
    }

    if(0 != redirect_backend_register("redirect")) {
        syslog(LOG_CRIT, "Couldn't register 'redirect' backend!");
        exit(EXIT_FAILURE);
    }
    if(0 != backend_switchto("redirect")) {
        syslog(LOG_CRIT, "Couldn't register 'redirect' backend!");
        exit(EXIT_FAILURE);
    }

    int listenfd = open_bind_socket(host, port);
    if(listenfd < 0) {
        syslog(LOG_CRIT, "Couldn't bind() socket!");
        exit(EXIT_FAILURE);
    }
    if(-1 == listen(listenfd, SOMAXCONN)) {
        syslog(LOG_CRIT, "listen: %m");
        exit(EXIT_FAILURE);
    }
    if(-1 == setnonblocking(listenfd)) {
        syslog(LOG_CRIT, "setnonblocking: %m");
        exit(EXIT_FAILURE);
    }

    opt = 1;
    setsockopt(listenfd, SOL_TCP, TCP_NODELAY, &opt, sizeof(opt));

    struct ev_loop *loop = EV_DEFAULT;
    ev_io listen_watcher;

    ev_io_init(&listen_watcher, accept_callback, listenfd, EV_READ);
    ev_io_start(loop, &listen_watcher);

    ev_run(loop, 0);

    return 0;
}

static int open_bind_socket(const char *addr, const char *port) {
    int ret, socketfd;
    struct addrinfo hints;
    struct addrinfo *result, *rp;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if(0 != (ret = getaddrinfo(addr, port, &hints, &result)) ) {
        syslog(LOG_CRIT, "getaddrinfo: %s", gai_strerror(ret));
        return ret;
    }

    for(rp = result; rp != NULL; rp = rp->ai_next) {
        socketfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if(-1 == socketfd)
            continue;

        int opt = 1;
        setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        if(-1 == (ret = bind(socketfd, rp->ai_addr, rp->ai_addrlen)) ) {
            syslog(LOG_ERR, "bind: %m");
        } else {
            break;
        }

        close_i(socketfd);
    }

    freeaddrinfo(result);

    if(NULL == rp){
        syslog(LOG_CRIT, "Couldn't bind %s:%s", addr, port);
        return -1;
    } else {
        return socketfd;
    }
}

int setnonblocking(int fd)
{
    int flags;
    if (-1 == (flags = fcntl(fd, F_GETFL, 0))) {
        flags = 0;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void accept_callback(EV_P_ ev_io *watcher, int revents) {
    int listenfd = watcher->fd;
    struct sockaddr_storage destaddr;

    int clientfd = accept(listenfd, NULL, NULL);
    if(-1 == clientfd) {
        syslog(LOG_ERR, "accept: %m");
        return;
    }
    if(-1 == setnonblocking(clientfd)) {
        syslog(LOG_ERR, "setnonblocking: %m");
        close_i(clientfd);
        return;
    }

    if(-1 == backend_getdestination(clientfd, &destaddr)){
        syslog(LOG_INFO, "backend_getdestination: %m");
        close_i(clientfd);
    }

    int destfd = socket(destaddr.ss_family, SOCK_STREAM, 0);
    if(-1 == destfd) {
        syslog(LOG_ERR, "accept: %m");
        return;
    }

    if(-1 == setnonblocking(destfd)) {
        syslog(LOG_ERR, "setnonblocking: %m");
        close_i(clientfd);
        close_i(destfd);
    }

    syslog(LOG_DEBUG, "accept_callback: connection accepted.");
    if(-1 == connect(destfd, (struct sockaddr *)(&destaddr), sizeof(destaddr))) {
        if(EINPROGRESS != errno) {
            syslog(LOG_ERR, "connect: %m");
            close_i(clientfd);
            close_i(destfd);
        }
    }

    ProxyContext *ctx = NULL;
    if(-1 == proxy_context_new(&ctx, clientfd, destfd)) {
        syslog(LOG_ERR, "Couldn't create proxy context!");
        close_i(clientfd);
        close_i(destfd);
        return;
    }
    proxy_context_start(loop, ctx);
}
