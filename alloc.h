#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <pthread.h>

#define packed __attribute__((__packed__))
#define unused __attribute__((__unused__))

#define MAXWORDS ((1024 * 1024 * 1024 / 4) - 2)
#define NUM_SIZE_CLASSES 8
#define THREAD_CACHE_SIZE 64  // blocks per size class per thread

#define HEADER_SIZE sizeof(header)
#define FOOTER_SIZE sizeof(footer)
#define OVERHEAD (HEADER_SIZE + FOOTER_SIZE)

#define err_no_mem 1
#define reterr(x) do { errno = (x); return (void *)0; } while(0)

typedef unsigned int int32;
typedef int32 word;

// block header - stores block metadata
struct s_header {
    word w;              // size in words (data region only, excludes header/footer)
    bool alloced;        // true if allocated, false if free
    word next_offset;    // offset to next block in free list (0 if not in list)
};
typedef struct s_header header;

// block footer
struct s_footer {
    word w;              // size in words (must match header)
    bool alloced;        // allocation status (must match header)
};
typedef struct s_footer footer;

#define GET_FOOTER(hdr) \
    ((footer *)((char *)(hdr) + HEADER_SIZE + ((hdr)->w * sizeof(word))))

#define GET_NEXT_HEADER(ftr) \
    ((header *)((char *)(ftr) + FOOTER_SIZE))

#define GET_PREV_FOOTER(hdr) \
    ((footer *)((char *)(hdr) - FOOTER_SIZE))

#define GET_HEADER_FROM_FOOTER(ftr) \
    ((header *)((char *)(ftr) - ((ftr)->w * sizeof(word)) - HEADER_SIZE))

// shared data, defined in alloc.c
extern const word SIZE_CLASS_LIMITS[NUM_SIZE_CLASSES];
extern header *free_lists[NUM_SIZE_CLASSES];

// memory pool - defined in heap.c
extern char memspace[];

// public api
void init_allocator(void);
void *alloc(int32 bytes);
void dealloc(void *ptr);
void show(header *hdr);