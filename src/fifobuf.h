#ifndef FIFOBUF_H
#define FIFOBUF_H

#include <stddef.h>

typedef struct {
    size_t          size;
    size_t          begin; 
    size_t          end; 
    unsigned char   data[];
} fifobuf_t;

fifobuf_t *fifobuf_new(size_t size);
#define fifobuf_delete(buf)     (free(buf))
size_t fifobuf_push_back(fifobuf_t *buf, const unsigned char *data, size_t size);
size_t fifobuf_pop_front(fifobuf_t *buf, unsigned char *data, size_t size);

#define fifobuf_capacity(buf)   (buf->size - buf->end)
#define fifobuf_amount(buf)     (buf->end - buf->begin)
#define fifobuf_buf(buf)        (buf->data + buf->begin)
#define fifobuf_space(buf)      (buf->data + buf->end)

#endif  /*  FIFOBUF_H */
