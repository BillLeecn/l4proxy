/*
 * fifobuf.c - implementing a FIFO buffer
 *
 * Copyright (c) 2015 Yang Li. All rights reserved.
 *
 * This program may be distributed according to the terms of the GNU
 * General Public License, version 3 or (at your option) any later version.
 */

#include <stdlib.h>
#include <string.h>

#include "fifobuf.h"

static void _shift_to_begin(fifobuf_t *buf);

fifobuf_t *fifobuf_new(size_t size) {
    fifobuf_t *obj = (fifobuf_t*)malloc(sizeof(fifobuf_t) + size);
    if(obj == NULL)
        return NULL;

    obj->size = size;
    obj->begin = 0;
    obj->end = 0;
    return obj;
}

size_t fifobuf_push_back(fifobuf_t *buf, const unsigned char *data, size_t size) {
    if(buf->end == size
            && buf->begin != 0) {
        _shift_to_begin(buf);
    }

    size_t capacity = fifobuf_capacity(buf);
    size_t ret = capacity>size? size: capacity;
    if(data != NULL)
        memcpy(buf->data + buf->end, data, ret);
    buf->end += ret;
    return ret;
}

size_t fifobuf_pop_front(fifobuf_t *buf, unsigned char *data, size_t size) {
    size_t amount = fifobuf_amount(buf);
    size_t ret = amount>size? size: amount;
    if(data != NULL)
        memcpy(data, buf->data + buf->begin, ret);
    buf->begin += ret;
    _shift_to_begin(buf);
    return ret;
}

static void  _shift_to_begin(fifobuf_t *buf) {
    memmove(buf->data, buf->data + buf->begin, fifobuf_amount(buf));
    buf->end -= buf->begin;
    buf->begin = 0;
}
