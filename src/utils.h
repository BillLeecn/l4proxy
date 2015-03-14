/*
 * utils - layer-4 proxy utils
 *
 * Copyright (c) 2015 Yang Li. All rights reserved.
 *
 * This program may be distributed according to the terms of the GNU
 * General Public License, version 3 or (at your option) any later version.
 */

#ifndef UTILS_H
#define UTILS_h

#include <unistd.h>

/* A close(2) failed with EINTR should not be restarted in linux.   */
#define close_i(fd)     (close(fd))

#endif  /*  UTILS_H */
