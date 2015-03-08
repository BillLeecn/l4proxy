#ifndef PROXY_H
#define PROXY_H

#include <ev.h>

#include "fifobuf.h"

typedef struct proxy_context {
    ev_io                   io;
    struct proxy_context    *dest;
    fifobuf_t               *buf;
} proxy_context;

int proxy_context_pair_new(proxy_context **pctx, int fd0, int fd1);
int proxy_context_pair_start(EV_P_ proxy_context *ctx);

#endif  /*  PROXY_H */
