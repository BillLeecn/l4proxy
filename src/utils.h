#ifndef UTILS_H
#define UTILS_h

#include <unistd.h>

/* A close(2) failed with EINTR should not be restarted in linux.   */
#define close_i(fd)     (close(fd))

#endif  /*  UTILS_H */
