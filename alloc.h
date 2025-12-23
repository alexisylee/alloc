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
#define MAXWORDS ((1024 * 1024 * 1024 / 4) - 2)
#define NUM_SIZE_CLASSES 8


#define GET_FOOTER(hdr) \
    ((footer *)((char *)(hdr) + 8 + ((hdr)->w * 4)))

#define GET_NEXT_HEADER(ftr) \
    ((header *)((char *)(ftr) + 4))

#define GET_PREV_FOOTER(hdr) \
    ((footer *)((char *)(hdr) - 4))

#define GET_HEADER_FROM_FOOTER(ftr) \
    ((header *)((char *)(ftr) - ((ftr)->w * 4) - 8))

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

// total 8 bytes now
struct packed s_header {
    word w: 30;
    bool alloced: 1;
    bool unused reserved: 1;
    word next_offset;
};
typedef struct packed s_header header;

struct packed s_footer {
    word w: 30;
    bool alloced: 1;
    bool unused reserved: 1;
};
typedef struct packed s_footer footer;

// size class boundaries (in words)
static const word SIZE_CLASS_LIMITS[NUM_SIZE_CLASSES] = {
    8,      // Class 0: 1-8 words
    16,     // Class 1: 9-16 words
    32,     // Class 2: 17-32 words
    64,     // Class 3: 33-64 words
    128,    // Class 4: 65-128 words
    256,    // Class 5: 129-256 words
    512,    // Class 6: 257-512 words
    ~0u     // Class 7: 513+ words (unlimited)
};

// free list heads (one per size class)
static header *free_lists[NUM_SIZE_CLASSES] = {NULL};

extern char memspace[];

void show(header*);
void *alloc(int32);
void free(void *);
