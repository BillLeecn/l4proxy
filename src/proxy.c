#include <assert.h>
#include <stdlib.h>

#include <errno.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "utils.h"
#include "proxy.h"

#define PROXY_BUFFER_SIZE   1024

static int proxy_context_pair_delete(EV_P_ proxy_context *ctx);
static void proxy_state_transition(EV_P_ proxy_context *ctx);

static void connect_callback(EV_P_ ev_io *watcher, int revents);
static void available_callback(EV_P_ ev_io *watcher, int revents);
static void read_available_callback(EV_P_ ev_io *watcher, int revents);
static void write_available_callback(EV_P_ ev_io *watcher, int revents);
static void connect_closed_callback(EV_P_ ev_io *watcher, int revents);

int proxy_context_pair_new(proxy_context **pctx, int fd0, int fd1) {
    proxy_context *ctx = (proxy_context*)malloc(2*sizeof(proxy_context));
    if(ctx == NULL) {
        syslog(LOG_ERR, "malloc: %m");
        return -1;
    }
    ev_io_init((ev_io*)ctx, NULL, fd0, 0);
    ev_io_init((ev_io*)(ctx+1), NULL, fd1, 0);

    ctx[0].dest = ctx+1;
    ctx[0].buf = NULL;
    ctx[1].dest = ctx;
    ctx[1].buf = NULL;

    *pctx = ctx;
    return 0;
}

static int proxy_context_pair_delete(EV_P_ proxy_context *ctx) {

    if(ctx->io.fd >= 0) {
        ev_io_stop(loop, &ctx->io);
        close_i(ctx->io.fd);
    }
    if(ctx->dest->io.fd >= 0) {
        ev_io_stop(loop, &ctx->dest->io);
        close_i(ctx->dest->io.fd);
    }

    if(ctx->buf)
        fifobuf_delete(ctx->buf);
    if(ctx->dest->buf)
        fifobuf_delete(ctx->dest->buf);

    if(ctx->dest < ctx)
        free(ctx->dest);
    else
        free(ctx);

    return 0;
}

int proxy_context_pair_start(EV_P_ proxy_context *ctx) {
    /*  wait for nonblocking connect(2) */
    ev_io_init(&ctx[1].io, connect_callback, ctx[1].io.fd, EV_WRITE);
    ev_io_start(loop, &ctx[1].io);
    return 0;
}

static void connect_callback(EV_P_ ev_io *watcher, int revents) {
    assert(EV_WRITE & revents);

    ev_io_stop(loop, watcher);
    proxy_context *ctx = (proxy_context*)watcher;

    int err = 0;
    socklen_t errlen = sizeof(err);
    if(-1 == getsockopt(watcher->fd, SOL_SOCKET, SO_ERROR, &err, &errlen)) {
        syslog(LOG_ERR, "getsockopt: %m");
        proxy_context_pair_delete(loop, ctx);
        return;
    }
    if(err) {
        syslog(LOG_INFO, "connect: %m");
        proxy_context_pair_delete(loop, ctx);
        return;
    }

    syslog(LOG_DEBUG, "connect_callback: remote connected.");
    if(NULL == (ctx->buf = fifobuf_new(PROXY_BUFFER_SIZE)) ) {
        syslog(LOG_ERR, "fifobuf_new failed! Cleaning up...");
        proxy_context_pair_delete(loop, ctx);
        return;
    }
    if(NULL == (ctx->dest->buf = fifobuf_new(PROXY_BUFFER_SIZE)) ) {
        syslog(LOG_ERR, "fifobuf_new failed! Cleaning up...");
        proxy_context_pair_delete(loop, ctx);
        return;
    }

    ev_io_init(&ctx->io, available_callback, ctx->io.fd, EV_READ);                 /*  remote side */
    ev_io_start(loop, &ctx->io);
    ev_io_init(&ctx->dest->io, available_callback, ctx->dest->io.fd, EV_READ);     /*  client side */
    ev_io_start(loop, &ctx->dest->io);
}

static void proxy_state_transition(EV_P_ proxy_context *ctx) {
    if(fifobuf_amount(ctx->buf)) {          /*  There is data to send.  */
        if(EV_WRITE & ctx->io.events) {
            /*  do nothing  */
        } else {                            /*  write not enabled   */
            syslog(LOG_DEBUG, "request sending...");
            assert(0 <= ctx->io.fd);
            ev_io_stop(loop, &ctx->io);
            ev_io_set(&ctx->io, ctx->io.fd, ctx->io.events | EV_WRITE);
            ev_io_start(loop, &ctx->io);
        }
    } else {                                /*  There is no data to send.   */
        if(EV_WRITE & ctx->io.events) {     /*  write enabled   */
            syslog(LOG_DEBUG, "Buffer empty. Sending paused.");
            assert(0 <= ctx->io.fd);
            ev_io_stop(loop, &ctx->io);
            ev_io_set(&ctx->io, ctx->io.fd, ctx->io.events & ~EV_WRITE);
            ev_io_start(loop, &ctx->io);
        } else {
            /*  do nothing  */
        }
    }
    if(fifobuf_capacity(ctx->buf)) {            /*  There is space to receive data. */
        if(EV_READ & ctx->dest->io.events) {
            /*  do nothing  */
        } else {                                /*  receive not enabled */
            syslog(LOG_DEBUG, "allow receiving...");
            assert(0 <= ctx->dest->io.fd);
            ev_io_stop(loop, &ctx->dest->io);
            ev_io_set(&ctx->dest->io, ctx->dest->io.fd, ctx->dest->io.events | EV_READ);
            ev_io_start(loop, &ctx->dest->io);
        }
    } else {                                    /*  There is no space to receive data.  */
        if(EV_READ & ctx->dest->io.events) {    /*  receive enabled */
            syslog(LOG_DEBUG, "Buffer full. Receiving paused.");
            assert(0 <= ctx->dest->io.fd);
            ev_io_stop(loop, &ctx->dest->io);
            ev_io_set(&ctx->dest->io, ctx->dest->io.fd, ctx->dest->io.events & ~EV_READ);
            ev_io_start(loop, &ctx->dest->io);
        } else {
            /*  do nothing  */
        }
    }
}

static void available_callback(EV_P_ ev_io *watcher, int revents) {
    if(EV_READ & revents)
        read_available_callback(loop, watcher, revents);
    if(EV_WRITE & revents)
        write_available_callback(loop, watcher, revents);
}

static void write_available_callback(EV_P_ ev_io *watcher, int revents) {
    syslog(LOG_DEBUG, "ready to send");
    proxy_context *ctx = (proxy_context*)watcher;

    if(fifobuf_amount(ctx->buf) > 0) {
        ssize_t nwrite;
        if(-1 == (nwrite = write(ctx->io.fd, fifobuf_buf(ctx->buf), fifobuf_amount(ctx->buf))) ) {
            if(EPIPE == errno) {
                syslog(LOG_DEBUG, "connection closed");
                connect_closed_callback(loop, watcher, revents);
                return;
            } else if(EAGAIN == errno || EWOULDBLOCK == errno){
                /*  do nothing  */
            } else {
                proxy_context_pair_delete(loop, ctx);
                return;
            }
        } else {
            syslog(LOG_DEBUG, "sending data...");
            fifobuf_pop_front(ctx->buf, NULL, nwrite);
        }
    } else {
        if(-1 == ctx->dest->io.fd) {
            syslog(LOG_DEBUG, "All data sent. Closing connection...");
            proxy_context_pair_delete(loop, ctx);
            return;
        }
    }

    proxy_state_transition(loop, ctx);
    assert(0 == fifobuf_amount(ctx->buf) || (EV_READ & ctx->dest->io.events));  /*  lock out condition  */
}

static void read_available_callback(EV_P_ ev_io *watcher, int revents) {
    syslog(LOG_DEBUG, "data pending...");
    proxy_context *ctx = (proxy_context*)watcher;

    if(fifobuf_capacity(ctx->dest->buf) > 0) {
        ssize_t nread;
        if(-1 == (nread = read(ctx->io.fd, fifobuf_space(ctx->dest->buf), fifobuf_capacity(ctx->dest->buf))) ) {
            if(EAGAIN != errno && EWOULDBLOCK != errno){
                proxy_context_pair_delete(loop, ctx);
                return;
            }
            /* else do nothing */
        } else if(0 == nread) {
            syslog(LOG_DEBUG, "connection closed");
            connect_closed_callback(loop, watcher, revents);
            return;
        } else {
            syslog(LOG_DEBUG, "receiving data...");
            fifobuf_push_back(ctx->dest->buf, NULL, nread);
        }
    } else {
        /*  do nothing  */
    }

    proxy_state_transition(loop, ctx->dest);
    assert(fifobuf_capacity(ctx->dest->buf) || (EV_WRITE & ctx->dest->io.events));  /*  lock out condition  */
}

static void connect_closed_callback(EV_P_ ev_io *watcher, int revents) {
    /*  TODO:   handle half-closed connection   */
    proxy_context *ctx = (proxy_context*)watcher;

    if(-1 == ctx->dest->io.fd){
        proxy_context_pair_delete(loop, ctx);
        return;
    }

    ev_io_stop(loop, watcher);
    close_i(watcher->fd);
    ev_io_set(watcher, -1, 0);

    if(0 == fifobuf_amount(ctx->dest->buf)) {
        proxy_context_pair_delete(loop, ctx);
        return;
    }

    if(ctx->dest->io.events & EV_READ) {
        ev_io_stop(loop, &ctx->dest->io);
        ev_io_set(&ctx->dest->io, ctx->dest->io.fd, ctx->dest->io.events & ~EV_READ);
        ev_io_start(loop, &ctx->dest->io);
    }

    assert( (ctx->dest->io.events & EV_WRITE) && fifobuf_amount(ctx->dest->buf) );
}
