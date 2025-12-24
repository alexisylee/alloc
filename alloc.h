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

#define HEADER_SIZE 8
#define FOOTER_SIZE 4
#define OVERHEAD (HEADER_SIZE + FOOTER_SIZE)

#define err_no_mem 1
#define reterr(x) do { errno = (x); return (void *)0; } while(0)

typedef unsigned int int32;
typedef int32 word;

// block header (8 bytes) - stores block metadata
struct packed s_header {
    word w: 30;              // size in words (data region only, excludes header/footer)
    bool alloced: 1;         // true if allocated, false if free
    bool unused reserved: 1; // reserved for future use
    word next_offset;        // offset to next block in free list (0 if not in list)
};
typedef struct packed s_header header;

// block footer (4 bytes) 
struct packed s_footer {
    word w: 30;              // size in words (must match header)
    bool alloced: 1;         // allocation status (must match header)
    bool unused reserved: 1; // reserved for future use
};
typedef struct packed s_footer footer;


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
// initialize allocator mutexes and state (call once before any alloc/free)
void init_allocator(void);
// allocate memory of specified size in bytes
void *alloc(int32 bytes);
// free previously allocated memory
void free(void *ptr);
// debug utility: display all blocks in memory
void show(header *hdr);
