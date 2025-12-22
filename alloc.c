#include "alloc.h"

extern char memspace[];

void *mkalloc(word words, header *hdr) {
    void *ret;
    ptrdiff_t bytesIn;
    word wordsIn;

    bytesIn = (char *)hdr - (char *)memspace;
    wordsIn = (bytesIn / 4) + 1;

    if (words > (MAXWORDS - wordsIn)) {
        reterr(ErrNoMem);
    }

    hdr->w = words;
    hdr->alloced = true;
    ret = (void *) hdr + 4;
    
    return ret;
}

// what is n?
// header *findblock(header *hdr, word n) {
//     if (n > MAXWORDS) {
//         reterr(ErrNoMem);
//     }

//     if (!(hdr->w)) {

//     } else {

//     }
// }

// allocates memory of bytes bytes, returns pointer to it
void *alloc(int32 bytes) {
    word words;
    header *hdr;
    void *mem; 

    if (bytes % 4) {
        words = (bytes / 4) + 1;
    } else {
        words = (bytes / 4);
    }

    mem = (void *) memspace;
    hdr = (header *) mem;

    // hdr->w = 2;

    // first time allocated
    if (!(hdr->w)) {
        if (words > MAXWORDS) {
            reterr(ErrNoMem);
        } 
        
        mem = mkalloc(words, hdr);
        if (!mem) {
            return (void *)(intptr_t) errno;
        }

        return mem;
    } else {
        // while haven't found unitialized space
        while (hdr->w != 0) {
            if (hdr->w >= words && !hdr->alloced) {
                // TODO consider splitting the block
                return mkalloc(words, hdr);
            }
            char *p = (char *)hdr + 4 + (hdr->w * 4);
            hdr = (header *)p;
        }
        // hit uninitialized space
        return mkalloc(words, hdr);
    }

    return NULL;
    
}

void free(void *ptr) {
    header *hdr = (header *) ((char *)ptr - 4);
    hdr->alloced = false;
}

void show(header *hdr) {
    header *p;
    void *mem;
    int32 n;
    for (n = 1, p = hdr; p->w; mem = (void *)p + ((p->w + 1) * 4), p = mem, n++) {
        printf("Alloc %d = %u %s words\n", n, p->w, (p->alloced) ? "alloced" : "freed");
    }
}

int main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;
    
    int8 *p;
    int8 *p2; 
    int8 *p3;

    printf("Memspace = 0x%x\n", (int8 *)memspace);
    p = alloc(7);
    // printf("Alloc1 = 0x%x\n", (int8 *)p);

    p2 = alloc(2000);
    // printf("Alloc2 = 0x%x\n", (int8 *)p2);

    p3 = alloc(1);
    
    // printf("Alloc3 = 0x%x\n", (int8 *)p3);
    show(memspace);
    free(p3);
    show(memspace);

    
    return 0;
}