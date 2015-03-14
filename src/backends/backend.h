/*
 * backends/backend.h - layer-4 proxy trasnsparent proxy module
 *
 * Copyright (c) 2015 Yang Li. All rights reserved.
 *
 * This program may be distributed according to the terms of the GNU
 * General Public License, version 3 or (at your option) any later version.
 */

#ifndef BACKENDS_BACKEND_H
#define BACKENDS_BACKEND_H

#include <sys/types.h>
#include <sys/socket.h>

int backend_getdestination(int fd, struct sockaddr_storage *addr);

typedef int (*getDestinationFn)(int, struct sockaddr_storage*);
int backend_register(const char name[], getDestinationFn);
int backend_switchto(const char name[]);

#endif  /*  BACKENDS_BACKEND_H  */
