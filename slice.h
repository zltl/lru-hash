#ifndef UTILS_SLICE_H_
#define UTILS_SLICE_H_

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* slice is a simple structure containing a pointer into some external storage
 * and a size. The user of a slice must ensure that the slice is not used after
 * the corresponding external storage has been deallocated.*/
struct slice_s {
    const uint8_t* data;
    size_t size;
};

typedef struct slice_s slice;

/* Create an empty slice. */
static inline slice empty_slice() {
    slice s;
    s.data = (uint8_t*)"";
    s.size = 0;
    return s;
}

/* create a slice that refers to data[0,n-1] */
static inline slice make_slice(const uint8_t* data, size_t size) {
    slice s;
    s.data = data;
    s.size = size;
    return s;
}

/* set slice by data and size. */
static inline void slice_set(slice* s, const uint8_t* data, size_t size) {
    s->data = data;
    s->size = size;
}

/* Create a slice that refers to s[0,strlen(s)-1] */
static inline slice slice_from_str(const char* s) {
    slice l;
    l.data = (uint8_t*)s;
    l.size = strlen(s);
    return l;
}

/* Return iff the length of the referenced data is zero. */
static inline int slice_empty(const slice* s) { return s->size == 0; }

/* CHange slice to refer to an empty array */
static inline void slice_clean(slice* s) { s->size = 0; }

/* Return the ith byte in the referenced data.
   REQUIRES: n < s->size. */
static inline uint8_t slice_char_at(const slice* s, size_t n) {
    assert(n < s->size);
    return s->data[n];
}

/* Drop the first n byte from slice. */
static inline void remove_prefix(slice* s, size_t n) {
    assert(n <= s->size);
    s->data += n;
    s->size -= n;
}

/* Return not zero iff contents of a and b are equal. */
static inline int slice_equal(const slice* a, const slice* b) {
    return ((a->size == b->size) && memcmp(a->data, b->data, a->size) == 0);
}

/* Three-way comparison. Return value:
                < 0 iff a < b,
                = 0 iff a = b,
                > 0 iff a > b  */
static inline int slice_compare(const slice* a, const slice* b) {
    const size_t min_len = (a->size < b->size) ? a->size : b->size;
    int r = memcmp(a->data, b->data, min_len);
    if (r == 0) {
        if (a->size < b->size)
            r = -1;
        else if (a->size > b->size)
            r = 1;
    }
    return r;
}

/* Return not zero iff b is a prefix of a. */
static inline int slice_start_with(const slice* a, const slice* b) {
    return ((a->size >= b->size) && (memcmp(a->data, b->data, b->size) == 0));
}

static inline void slice_move(slice* dst, const slice* src) {
    memcpy(dst, src, sizeof(slice));
}

static inline void slice_copy(uint8_t* dst, size_t dst_len, const slice* src) {
    assert(dst_len >= src->size);
    if (dst_len >= src->size)
        memcpy(dst, src->data, src->size);
    else
        memcpy(dst, src->data, dst_len);
}

static inline int slice_conv_int(const slice* s) {
    int num = 0;
    size_t i = 0;
    while (i < s->size) {
        int c = (int)*(s->data + i) - '0';
        num = num * 10 + c;
        ++i;
    }
    return num;
}

#include <ctype.h>
#include <stdio.h>
static inline int slice_print(const slice* s) {
    size_t i;
    for (i = 0; i < s->size; i++) {
        if (isprint(s->data[i]))
            fprintf(stdout, "%c ", s->data[i]);
        else
            fprintf(stdout, "%02x ", s->data[i]);
    }
    return 0;
}

#endif /* UTILS_SLICE_H_ */
