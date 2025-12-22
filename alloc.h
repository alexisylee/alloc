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

#define ErrNoMem 1

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

#define reterr(x) errno = (x); return (void *) 0

void show(header*);
void *mkalloc(word, header *);
void *alloc(int32);
void free(void *);
int main(int, char**);
