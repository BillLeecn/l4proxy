#ifndef PROXY_H
#define PROXY_H

typedef struct proxy_context_t ProxyContext;

int proxy_context_new(ProxyContext **pctx, int clientfd, int remotefd);
int proxy_context_start(EV_P_ ProxyContext *ctx);

#endif  /*  PROXY_H */
