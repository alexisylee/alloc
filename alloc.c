#include "alloc.h"

pthread_mutex_t alloc_mutex = PTHREAD_MUTEX_INITIALIZER;

static inline word ptr_to_offset(header *ptr) {
    if (ptr == NULL) return 0;
    return (word)((char *)ptr - (char *)memspace);
}

static inline header *offset_to_ptr(word offset) {
    if (offset == 0) return NULL;
    return (header *)((char *)memspace + offset);
}

// get size class for a given word count
int get_size_class(word words) {
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        if (words <= SIZE_CLASS_LIMITS[i]) {
            return i;
        }
    }
    return NUM_SIZE_CLASSES - 1;
}

// add block to appropriate free list
void add_to_free_list(header *hdr) {
    int class = get_size_class(hdr->w);
    
    // insert at head of list
    hdr->next_offset = ptr_to_offset(free_lists[class]);
    free_lists[class] = hdr;
}

// remove block from free list
void remove_from_free_list(header *hdr) {
    int class = get_size_class(hdr->w);
    
    if (free_lists[class] == hdr) {
        free_lists[class] = offset_to_ptr(hdr->next_offset);
    } else {
        header *curr = free_lists[class];
        while (curr && offset_to_ptr(curr->next_offset) != hdr) {
            curr = offset_to_ptr(curr->next_offset);
        }
        if (curr) {
            curr->next_offset = hdr->next_offset;
        }
    }
    
    hdr->next_offset = 0;
}

// find free block in size class (first-fit within class)
header *find_free_block(word words) {
    int class = get_size_class(words);
    
    for (int i = class; i < NUM_SIZE_CLASSES; i++) {
        header *curr = free_lists[i];
        while (curr) {
            if (curr->w >= words) return curr;
            curr = offset_to_ptr(curr->next_offset);
        }
    }
    return NULL;
}

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
    ret = (void *)((char *)hdr + 8);

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
    void *ret = (void *)((char *)hdr + 8);
    
    // create remainder block
    footer *ftr = GET_FOOTER(hdr);
    header *new_hdr = (header *)((char *)ftr + 4);
    new_hdr->reserved = 0;
    set_block(new_hdr, old_size - requested_words - 3, false);

    add_to_free_list(new_hdr);
    
    return ret;
}
// allocates memory of bytes bytes, returns pointer to it
void *alloc(int32 bytes) {
    pthread_mutex_lock(&alloc_mutex);
    word words;
    void *mem; 

    if (bytes % 4) {
        words = (bytes / 4) + 1;
    } else {
        words = (bytes / 4);
    }

    header *hdr = find_free_block(words);
    
    if (hdr) {
        printf("[ALLOC] Found free block at %p, size %u\n", (void *)hdr, hdr->w);
        // found a free block
        remove_from_free_list(hdr);
        
        if (hdr->w >= words + 4) {
            mem = split_block(hdr, words);
            printf("[ALLOC] Split block, returning %p\n", mem);
            pthread_mutex_unlock(&alloc_mutex);
            return mem;
        } else {
            set_block(hdr, hdr->w, true);
            mem = (void *)((char *)hdr + 8);
            printf("[ALLOC] Using whole block, returning %p\n", mem);
            pthread_mutex_unlock(&alloc_mutex);
            return mem;
        }
    }
    
    // no free block - allocate at end of heap
    hdr = (header *)memspace;
    
    // find first uninitialized space
    while (hdr->w != 0) {
        footer *ftr = GET_FOOTER(hdr);
        hdr = GET_NEXT_HEADER(ftr);
    }
    
    if (words > MAXWORDS) {
        reterr(ErrNoMem);
    }
    
    void *m = mkalloc(words, hdr);
    pthread_mutex_unlock(&alloc_mutex);
    return m;
}

void free(void *ptr) {
    pthread_mutex_lock(&alloc_mutex);

    if (ptr == NULL) {
        pthread_mutex_unlock(&alloc_mutex);
        return;
    }

    header *hdr = (header *) ((char *)ptr - 8);
    footer *ftr = GET_FOOTER(hdr);
    hdr->alloced = false;
    ftr->alloced = false;
    printf("[FREE] Freeing %p (header at %p, size %u)\n", ptr, (void *)hdr, hdr->w);
    // forward coalesce
    header *next = (header *)((char *)ftr + 4);
    
    if (next->w != 0 && !next->alloced) {
        remove_from_free_list(next);

        hdr->w += next->w + 3;
        ftr = GET_FOOTER(hdr);
        ftr->w = hdr->w;
        ftr->alloced = false;
    }

    // backward coalesce
    if ((char *)hdr > (char *)memspace) {
        footer *prev_ftr = GET_PREV_FOOTER(hdr);
    
        if (prev_ftr->w != 0 && !prev_ftr->alloced) {
            header *prev_hdr = GET_HEADER_FROM_FOOTER(prev_ftr);
            remove_from_free_list(prev_hdr);

            prev_hdr->w += hdr->w + 3;
            ftr = GET_FOOTER(prev_hdr);
            ftr->w = prev_hdr->w;
            ftr->alloced = false;

            hdr = prev_hdr;
        }
    }
    printf("[FREE] Adding %p to free list (size %u)\n", (void *)hdr, hdr->w);
    add_to_free_list(hdr);
    pthread_mutex_unlock(&alloc_mutex);
}

void show(header *hdr) {
    if (hdr == NULL) return;

    header *p = hdr;
    int32 n = 1;
    
    while (p->w != 0) {
        printf("Alloc %d = %u %s words at %p\n", 
               n, p->w, 
               (p->alloced) ? "alloced" : "freed", 
               (void *)p);
        
        footer *ftr = GET_FOOTER(p);
        p = GET_NEXT_HEADER(ftr);
        
        printf("Next block is %p\n\n", (void *)p);
        n++;
    }
}

