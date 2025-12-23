#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include <stddef.h>

#define packed __attribute__((__packed__))
#define unused __attribute__((__unused__))
#define MAXWORDS ((1024 * 1024 * 1024 / 4) - 1)

#define GET_FOOTER(hdr) \
    ((footer *)((char *)(hdr) + 4 + ((hdr)->w * 4)))

#define GET_NEXT_HEADER(ftr) \
    ((header *)((char *)(ftr) + 4))

#define GET_PREV_FOOTER(hdr) \
    ((footer *)((char *)(hdr) - 4))

#define GET_HEADER_FROM_FOOTER(ftr) \
    ((header *)((char *)(ftr) - ((ftr)->w * 4) - 4))

// TODO change case
#define ErrNoMem 1

#define reterr(x) errno = (x); return (void *) 0

typedef unsigned char int8;
typedef unsigned short int int16;
typedef unsigned int int32;
typedef unsigned long long int int64;
typedef unsigned __int128 int128;
typedef void heap;
typedef int32 word;

struct packed s_header {
    word w: 30;
    bool alloced: 1;
    bool unused reserved: 1;
};
typedef struct packed s_header header;

struct packed s_footer {
    word w: 30;
    bool alloced: 1;
    bool unused reserved: 1;
};
typedef struct packed s_footer footer;


extern char memspace[];

void show(header*);
void *alloc(int32);
void free(void *);
