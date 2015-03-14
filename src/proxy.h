/*
 * proxy.h - layer-4 proxy proxy context module
 *
 * Copyright (c) 2015 Yang Li. All rights reserved.
 *
 * This program may be distributed according to the terms of the GNU
 * General Public License, version 3 or (at your option) any later version.
 */

#ifndef PROXY_H
#define PROXY_H

typedef struct proxy_context_t ProxyContext;

int proxy_context_new(ProxyContext **pctx, int clientfd, int remotefd);
int proxy_context_start(EV_P_ ProxyContext *ctx);

#endif  /*  PROXY_H */
