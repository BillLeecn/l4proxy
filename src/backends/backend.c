#include <string.h>

#include "backend.h"

static char *s_name;
static getDestinationFn s_getdestination;

int backend_getdestination(int fd, struct sockaddr_storage *addr) {
    return (*s_getdestination)(fd, addr);
}

int backend_register(const char name[], getDestinationFn fn) {
    if(s_name != NULL)
        return -1;

    s_name = strdup(name);
    s_getdestination = fn;
    return 0;
}

int backend_switchto(const char name[]) {
    if(NULL == s_name)
        return -1;

    if(0 == strcmp(s_name, name))
        return 0;
    else
        return -1;
}
