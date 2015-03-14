/*
 * backends/redirect.h - layer-4 proxy trasnsparent proxy with redirect module
 *
 * Copyright (c) 2015 Yang Li. All rights reserved.
 *
 * This program may be distributed according to the terms of the GNU
 * General Public License, version 3 or (at your option) any later version.
 */

#ifndef BACKENDS_REDIRECT_H
#define BACKENDS_REDIRECT_H

int redirect_backend_register(const char name[]);

#endif  /*  BACKENDS_REDIRECT_H */
