/*
 * proxy.c - layer-4 proxy proxy context module
 *
 * Copyright (c) 2015 Yang Li. All rights reserved.
 *
 * This program may be distributed according to the terms of the GNU
 * General Public License, version 3 or (at your option) any later version.
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <ev.h>

#include "utils.h"
#include "fifobuf.h"
#include "proxy.h"

#define PROXY_BUFFER_SIZE   2048

typedef struct read_context_t ReadContext;
typedef struct write_context_t WriteContext;

struct read_context_t {
    ev_io           io;
    fifobuf_t       *buf;
    WriteContext    *dst;
    ProxyContext    *proxy;
    int             connected;
};

struct write_context_t {
    ev_io           io;
    fifobuf_t       *buf;
    ReadContext     *src;
    ProxyContext    *proxy;
    int             connected;
};

struct proxy_context_t {
    ReadContext     client_read_ctx;
    WriteContext    client_write_ctx;
    ReadContext     remote_read_ctx;
    WriteContext    remote_write_ctx;
};

static int proxy_context_delete(EV_P_ ProxyContext *ctx);
static void state_transist(EV_P_ ProxyContext *ctx);

static void read_callback(EV_P_ ev_io *watcher, int revents);
static void write_callback(EV_P_ ev_io *watcher, int revents);

static void connect_callback(EV_P_ ev_io *watcher, int revents);
static void disconnect_callback(EV_P_ ev_io *watcher, int revents);

int proxy_context_new(ProxyContext **pctx, int fd0, int fd1) {
    ProxyContext *ctx = (ProxyContext*)malloc(sizeof(ProxyContext));
    if(NULL == ctx) {
        syslog(LOG_ERR, "malloc failed");
        *pctx = NULL;
        return -1;
    }
    memset(ctx, 0, sizeof(ProxyContext));

    ctx->client_read_ctx.dst = &ctx->remote_write_ctx;
    ctx->client_write_ctx.src = &ctx->remote_read_ctx;
    ctx->remote_read_ctx.dst = &ctx->client_write_ctx;
    ctx->remote_write_ctx.src = &ctx->client_read_ctx;

    ctx->client_read_ctx.proxy = ctx;
    ctx->client_write_ctx.proxy = ctx;
    ctx->remote_read_ctx.proxy = ctx;
    ctx->remote_write_ctx.proxy = ctx;

    ev_io_init(&ctx->client_read_ctx.io, &read_callback, fd0, EV_READ);
    ev_io_init(&ctx->client_write_ctx.io, &write_callback, fd0, EV_WRITE);
    ev_io_init(&ctx->remote_read_ctx.io, &read_callback, fd1, EV_READ);
    ev_io_init(&ctx->remote_write_ctx.io, &write_callback, fd1, EV_WRITE);

    *pctx = ctx;
    return 0;
}

int proxy_context_start(EV_P_ ProxyContext *ctx) {
    ctx->client_read_ctx.connected = 1;
    ctx->client_write_ctx.connected = 1;

    assert(EV_WRITE & ctx->remote_write_ctx.io.events);
    ev_io_start(loop, &ctx->remote_write_ctx.io);
    return 0;
}

static void read_callback(EV_P_ ev_io *watcher, int revents) {
    ReadContext *ctx = (ReadContext*)watcher;
    ProxyContext *proxy = ctx->proxy;

    ssize_t nread;
    if(-1 == (nread = read(ctx->io.fd, fifobuf_space(ctx->buf), fifobuf_capacity(ctx->buf))) ) {
        if(EAGAIN == errno || EWOULDBLOCK == errno) {
            /*  do nothing  */
        } else {
            syslog(LOG_ERR, "<%p> read: %m", proxy);
            proxy_context_delete(loop, proxy);
            return;
        }
    } else if(0 == nread) {
        disconnect_callback(loop, watcher, revents);
        return;
    } else {
        fifobuf_push_back(ctx->buf, NULL, nread);
        state_transist(loop, proxy);
    }
}

static void write_callback(EV_P_ ev_io *watcher, int revents) {
    WriteContext *ctx = (WriteContext*)watcher;
    ProxyContext *proxy = ctx->proxy;

    if(!ctx->connected) {
        connect_callback(loop, watcher, revents);
        return;
    }

    ssize_t nwrite;
    if(-1 == (nwrite = write(ctx->io.fd, fifobuf_buf(ctx->buf), fifobuf_amount(ctx->buf))) ) {
        if(EPIPE == errno) {
            disconnect_callback(loop, watcher, revents);
            return;
        } else if(EAGAIN == errno || EWOULDBLOCK == errno) {
            /*  do nothing  */
        } else {
            syslog(LOG_ERR, "<%p> write: %m", proxy);
            proxy_context_delete(loop, proxy);
            return;
        }
    } else {
        fifobuf_pop_front(ctx->buf, NULL, nwrite);
        state_transist(loop, proxy);
    }
}

static void connect_callback(EV_P_ ev_io *watcher, int revents) {
    WriteContext *ctx = (WriteContext*)watcher;
    ProxyContext *proxy = ctx->proxy;

    int err = 0;
    socklen_t errlen = sizeof(err);
    if(-1 == getsockopt(watcher->fd, SOL_SOCKET, SO_ERROR, &err, &errlen)) {
        syslog(LOG_ERR, "<%p> getsockopt: %m", proxy);
        proxy_context_delete(loop, proxy);
        return;
    }
    if(err) {
        syslog(LOG_INFO, "<%p> connect: %s", proxy, strerror(err));
        proxy_context_delete(loop, proxy);
        return;
    }

    proxy->remote_read_ctx.connected = 1;
    proxy->remote_write_ctx.connected = 1;
    syslog(LOG_DEBUG, "<%p> connect_callback: remote connected", proxy);
    if(NULL == (proxy->client_read_ctx.buf = proxy->remote_write_ctx.buf = fifobuf_new(PROXY_BUFFER_SIZE)) ) {
        syslog(LOG_ERR, "fifobuf_new failed! Cleaning up...");
        return;
    }
    if(NULL == (proxy->client_write_ctx.buf = proxy->remote_read_ctx.buf = fifobuf_new(PROXY_BUFFER_SIZE)) ) {
        syslog(LOG_ERR, "fifobuf_new failed! Cleaning up...");
        return;
    }

    ev_io_start(loop, &proxy->client_read_ctx.io);
    ev_io_start(loop, &proxy->remote_read_ctx.io);
    ev_io_stop(loop, &proxy->remote_write_ctx.io);
}

static int proxy_context_delete(EV_P_ ProxyContext *ctx) {
    ev_io_stop(loop, &ctx->client_read_ctx.io);
    ev_io_stop(loop, &ctx->client_write_ctx.io);
    ev_io_stop(loop, &ctx->remote_read_ctx.io);
    ev_io_stop(loop, &ctx->remote_write_ctx.io);

    if(ctx->client_read_ctx.connected || ctx->client_write_ctx.connected) {
        syslog(LOG_DEBUG, "<%p> proxy_context_delete: closing client side...", ctx);
        close_i(ctx->client_read_ctx.io.fd);
    }
    if(ctx->remote_read_ctx.connected || ctx->remote_write_ctx.connected) {
        syslog(LOG_DEBUG, "<%p> proxy_context_delete: closing remote side...", ctx);
        close_i(ctx->remote_read_ctx.io.fd);
    }

    if(ctx->client_read_ctx.buf) {
        fifobuf_delete(ctx->client_read_ctx.buf);
    }
    if(ctx->client_write_ctx.buf) {
        fifobuf_delete(ctx->client_write_ctx.buf);
    }

    free(ctx);
    return 0;
}

static void disconnect_callback(EV_P_ ev_io *watcher, int revents) {
    ProxyContext *proxy;

    if(EV_WRITE & revents) {
        WriteContext *ctx = (WriteContext*)watcher;
        proxy = ctx->proxy;

        ctx->connected = 0;
    } else if (EV_READ & revents) {
        ReadContext *ctx = (ReadContext*)watcher;
        proxy = ctx->proxy;

        ctx->connected = 0;
    } else {
        syslog(LOG_CRIT, "disconnect_callback: neither EV_WRITE nore EV_READ is set.");
        exit(EXIT_FAILURE);
    }

    int client_disconnected = !(proxy->client_read_ctx.connected && proxy->client_write_ctx.connected);
    int remote_disconnected = !(proxy->remote_read_ctx.connected && proxy->remote_write_ctx.connected);

    if(client_disconnected) {
        syslog(LOG_DEBUG, "<%p> disconnect_callback: client disconnected.", proxy);
        proxy->client_read_ctx.connected = proxy->client_write_ctx.connected = 0;
        close_i(proxy->client_read_ctx.io.fd);
    }
    if(remote_disconnected) {
        syslog(LOG_DEBUG, "<%p> disconnect_callback: remote disconnected.", proxy);
        proxy->remote_read_ctx.connected = proxy->remote_write_ctx.connected = 0;
        close_i(proxy->remote_read_ctx.io.fd);
    }

    if(
        (client_disconnected && remote_disconnected)
        || (client_disconnected && (0 == fifobuf_amount(proxy->remote_write_ctx.buf)))
        || (remote_disconnected && (0 == fifobuf_amount(proxy->client_write_ctx.buf)))
      ) {
        syslog(LOG_DEBUG, "<%p> disconnect_callback: releasing proxy context.", proxy);
        proxy_context_delete(loop, proxy);
        return;
    }

    state_transist(loop, proxy);
}

static void state_transist(EV_P_ ProxyContext *ctx) {
    if(!(ctx->client_read_ctx.connected && ctx->client_read_ctx.dst->connected)) {
        ev_io_stop(loop, &ctx->client_read_ctx.io);
    } else if(fifobuf_capacity(ctx->client_read_ctx.buf)) {
        ev_io_start(loop, &ctx->client_read_ctx.io);
    } else {
        ev_io_stop(loop, &ctx->client_read_ctx.io);
    }

    if(!ctx->client_write_ctx.connected) {
        ev_io_stop(loop, &ctx->client_write_ctx.io);
    } else if(fifobuf_amount(ctx->client_write_ctx.buf)) {
        ev_io_start(loop, &ctx->client_write_ctx.io);
    } else {
        ev_io_stop(loop, &ctx->client_write_ctx.io);
    }

    if(!(ctx->remote_read_ctx.connected && ctx->remote_read_ctx.dst->connected)) {
        ev_io_stop(loop, &ctx->remote_read_ctx.io);
    } else if(fifobuf_capacity(ctx->remote_read_ctx.buf)) {
        ev_io_start(loop, &ctx->remote_read_ctx.io);
    } else {
        ev_io_stop(loop, &ctx->remote_read_ctx.io);
    }
    if(!ctx->remote_write_ctx.connected) {
        ev_io_stop(loop, &ctx->remote_write_ctx.io);
    } else if(fifobuf_amount(ctx->remote_write_ctx.buf)) {
        ev_io_start(loop, &ctx->remote_write_ctx.io);
    } else {
        ev_io_stop(loop, &ctx->remote_write_ctx.io);
    }
}
