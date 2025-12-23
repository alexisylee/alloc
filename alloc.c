#include "alloc.h"


void *mkalloc(word words, header *hdr) {
    // printf("Allocating %d words at address 0x%d\n", words, hdr);
    if (hdr == NULL) return NULL;

    void *ret;
    ptrdiff_t bytesIn;
    word wordsIn;

    bytesIn = (char *)hdr - (char *)memspace;
    wordsIn = (bytesIn / 4);

    if (words > (MAXWORDS - wordsIn)) {
        reterr(ErrNoMem);
    }

    hdr->w = words;
    hdr->alloced = true;
    
    // return pointer to right after header
    ret = (void *)((char *)hdr + 4);

    footer *ftr = GET_FOOTER(hdr);
    ftr->w = words;
    ftr->alloced = true;
    
    return ret;
}

void set_block(header *hdr, word size, bool is_alloced) {
    hdr->w = size;
    hdr->alloced = is_alloced;
    
    footer *ftr = GET_FOOTER(hdr);
    ftr->w = size;
    ftr->alloced = is_alloced;
    ftr->reserved = 0;
}

// split block: returns pointer to data of first block
void *split_block(header *hdr, word requested_words) {
    word old_size = hdr->w;
    
    // set up allocated block
    set_block(hdr, requested_words, true);
    void *ret = (void *)((char *)hdr + 4);
    
    // create remainder block
    footer *ftr = GET_FOOTER(hdr);
    header *new_hdr = (header *)((char *)ftr + 4);
    new_hdr->reserved = 0;
    set_block(new_hdr, old_size - requested_words - 2, false);
    
    return ret;
}
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

    mem = (void *)memspace;
    hdr = (header *)mem;
    
    // search for free block
    while (hdr->w != 0) {
        if (!hdr->alloced && hdr->w >= words) {
            // found free block big enough
            if (hdr->w >= words + 2) {
                return split_block(hdr, words);
            } else {
                set_block(hdr, hdr->w, true);
                return (void *)((char *)hdr + 4);
            }
        }
        // increment
        footer *ftr = GET_FOOTER(hdr);
        hdr = GET_NEXT_HEADER(ftr);
    }
    
    // hit uninitialized space
    return mkalloc(words, hdr);
}

void free(void *ptr) {
    if (ptr == NULL) return;

    header *hdr = (header *) ((char *)ptr - 4);
    footer *ftr = GET_FOOTER(hdr);
    hdr->alloced = false;
    ftr->alloced = false;

    // forward coalesce
    header *next = (header *)((char *)ftr + 4);
    
    if (next->w != 0 && !next->alloced) {
        hdr->w += next->w + 2;
        ftr = GET_FOOTER(hdr);
        ftr->w = hdr->w;
        ftr->alloced = false;
    }

    // backward coalesce
    if ((char *)hdr > (char *)memspace) {
        footer *prev_ftr = GET_PREV_FOOTER(hdr);
    
        if (prev_ftr->w != 0 && !prev_ftr->alloced) {
            header *prev_hdr = GET_HEADER_FROM_FOOTER(prev_ftr);
            prev_hdr->w += hdr->w + 2;

            ftr = GET_FOOTER(prev_hdr);
            ftr->w = prev_hdr->w;
            ftr->alloced = false;
        }
    }
}

void show(header *hdr) {
    if (hdr == NULL) return;

    header *p;
    void *mem;
    int32 n;
    for (n = 1, p = hdr; p->w; mem = (char *)p + ((p->w + 2) * 4), p = mem, n++) {
        printf("Alloc %d = %u %s words at %d\n", n, p->w, (p->alloced) ? "alloced" : "freed", p);
        printf("Next block is 0x%d\n\n", (char *)p + ((p->w + 2) * 4));
    }
}

