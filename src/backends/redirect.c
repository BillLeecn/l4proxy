#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <linux/netfilter_ipv4.h>

#include "backend.h"

static int getorigdest(int fd, struct sockaddr_storage *addr){
    socklen_t len = sizeof(*addr);
    return getsockopt(fd, SOL_IP, SO_ORIGINAL_DST, addr, &len);
}

int redirect_backend_register(const char name[]){
    if(NULL == name)
        name = "redirect";
    return backend_register(name, getorigdest);
}
