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
                // split
                if (hdr->w >= words + 2) {
                    word oldSize = hdr->w;

                    hdr->w = words;
                    hdr->alloced = true;
                    void *ret = (void *)((char *)hdr + 4);
                
                    // create new header for remaining space
                    header *newHdr = (header *)((char *)hdr + 4 + (words * 4));
                    newHdr->w = oldSize - words - 1;  // subtract allocated + header
                    newHdr->alloced = false;
                    newHdr->reserved = 0;
                    
                    return ret;
                } else {
                    hdr->alloced = true;
                    return (void *)((char *)hdr + 4);
                }
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

    // coalesce
    header *next = (header *)((char *)hdr + 4 + (hdr->w * 4));
    
    printf("Hdr address: 0x%d\n", hdr);
    while (next->w != 0 && !next->alloced) {
        printf("Next address: 0x%d\n", next);
        hdr->w += next->w + 1;
        next = (header *)((char *)next + 4 + (next->w * 4));
    }
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
    
    int8 *p1 = alloc(40);   // 10 words
    int8 *p2 = alloc(80);   // 20 words
    int8 *p3 = alloc(120);  // 30 words
    int8 *p4 = alloc(160);  // 40 words
    
    printf("=== Initial ===\n");
    show((header *)memspace);
    
    printf("\n=== Free p1, p2, p3 (should coalesce) ===\n");
    free(p1);
    free(p2);
    free(p3);
    
    show((header *)memspace);

    
    return 0;
}