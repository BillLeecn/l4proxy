#ifndef BACKENDS_BACKEND_H
#define BACKENDS_BACKEND_H

#include <sys/types.h>
#include <sys/socket.h>

int backend_getdestination(int fd, struct sockaddr_storage *addr);

typedef int (*getDestinationFn)(int, struct sockaddr_storage*);
int backend_register(const char name[], getDestinationFn);
int backend_switchto(const char name[]);

#endif  /*  BACKENDS_BACKEND_H  */
