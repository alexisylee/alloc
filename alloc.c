#include "alloc.h"

// global state
// size class boundaries (in words) - determines which free list a block belongs to
const word SIZE_CLASS_LIMITS[NUM_SIZE_CLASSES] = {
    8,      // class 0: 1-8 words
    16,     // class 1: 9-16 words
    32,     // class 2: 17-32 words
    64,     // class 3: 33-64 words
    128,    // class 4: 65-128 words
    256,    // class 5: 129-256 words
    512,    // class 6: 257-512 words
    ~0u     // class 7: 513+ words (unlimited)
};

// segregated free lists - one per size class
header *free_lists[NUM_SIZE_CLASSES] = {NULL};

// per-size-class mutexes for fine-grained locking
pthread_mutex_t size_class_locks[NUM_SIZE_CLASSES];

// mutex for heap expansion (allocating from new memory)
pthread_mutex_t heap_expand_lock;

// track end of allocated heap - only modified under heap_expand_lock
static header *heap_top = NULL;


#define WORDS_TO_BYTES(w) ((w) * sizeof(word))
#define BYTES_TO_WORDS(b) (((b) + sizeof(word) - 1) / sizeof(word))
#define OVERHEAD_WORDS (OVERHEAD / sizeof(word)) 


static inline word ptr_to_offset(header *ptr) {
    if (ptr == NULL) return 0;
    return (word)((char *)ptr - (char *)memspace);
}

static inline header *offset_to_ptr(word offset) {
    if (offset == 0) return NULL;
    return (header *)((char *)memspace + offset);
}

static inline int get_size_class(word words) {
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        if (words <= SIZE_CLASS_LIMITS[i]) {
            return i;
        }
    }
    return NUM_SIZE_CLASSES - 1;
}

// add block to appropriate free list (assumes caller holds correct lock)
static void add_to_free_list(header *hdr) {
    int class = get_size_class(hdr->w);
    hdr->next_offset = ptr_to_offset(free_lists[class]);
    free_lists[class] = hdr;
}

// try to remove block from free list - returns true if found
static bool remove_from_free_list_checked(header *hdr) {
    int class = get_size_class(hdr->w);

    if (free_lists[class] == hdr) {
        free_lists[class] = offset_to_ptr(hdr->next_offset);
        hdr->next_offset = 0;
        return true;
    }

    header *curr = free_lists[class];
    while (curr && offset_to_ptr(curr->next_offset) != hdr) {
        curr = offset_to_ptr(curr->next_offset);
    }

    if (curr) {
        curr->next_offset = hdr->next_offset;
        hdr->next_offset = 0;
        return true;
    }

    return false; 
}

static void remove_from_free_list(header *hdr) {
    remove_from_free_list_checked(hdr);
}


// acquire neighbor's lock respecting lock ordering
// returns true if we needed to acquire a different lock
static bool acquire_neighbor_lock_ordered(int current_class, int neighbor_class) {
    if (neighbor_class == current_class) {
        return false;  // already holding the right lock
    }

    if (neighbor_class < current_class) {
        // must unlock current, lock neighbor, relock current
        pthread_mutex_unlock(&size_class_locks[current_class]);
        pthread_mutex_lock(&size_class_locks[neighbor_class]);
        pthread_mutex_lock(&size_class_locks[current_class]);
    } else {
        // neighbor_class > current_class: just lock neighbor
        pthread_mutex_lock(&size_class_locks[neighbor_class]);
    }

    return true;
}


// initialize block header and footer
static void set_block_metadata(header *hdr, word size, bool is_alloced) {
    hdr->w = size;
    hdr->alloced = is_alloced;

    footer *ftr = GET_FOOTER(hdr);
    ftr->w = size;
    ftr->alloced = is_alloced;
    ftr->reserved = 0;
}

// allocate a new block from uninitialized memory
// assumes caller holds heap_expand_lock
static void *allocate_from_fresh_memory(word words, header *hdr) {
    if (hdr == NULL) return NULL;

    ptrdiff_t bytes_in = (char *)hdr - (char *)memspace;
    word words_in = BYTES_TO_WORDS(bytes_in);

    if (words > (MAXWORDS - words_in)) {
        reterr(err_no_mem);
    }

    hdr->w = words;
    hdr->alloced = true;

    footer *ftr = GET_FOOTER(hdr);
    ftr->w = words;
    ftr->alloced = true;

    return (void *)((char *)hdr + HEADER_SIZE);
}

// split block into allocated part and free remainder
// caller must hold lock for remainder's size class
static void *split_block(header *hdr, word requested_words) {
    word old_size = hdr->w;

    // set up allocated block
    set_block_metadata(hdr, requested_words, true);
    void *ret = (void *)((char *)hdr + HEADER_SIZE);

    // create remainder block
    footer *ftr = GET_FOOTER(hdr);
    header *remainder = (header *)((char *)ftr + FOOTER_SIZE);
    remainder->reserved = 0;
    set_block_metadata(remainder, old_size - requested_words - OVERHEAD_WORDS, false);

    // add remainder to free list (caller ensures appropriate lock is held)
    add_to_free_list(remainder);

    return ret;
}


void *alloc(int32 bytes) {
    word words = BYTES_TO_WORDS(bytes);
    int target_class = get_size_class(words);

    // search free lists for suitable block
    for (int i = target_class; i < NUM_SIZE_CLASSES; i++) {
        pthread_mutex_lock(&size_class_locks[i]);

        header *hdr = free_lists[i];
        while (hdr) {
            if (hdr->w >= words) {
                remove_from_free_list(hdr);

                // check if we should split
                if (hdr->w >= words + OVERHEAD_WORDS + 1) {  // +1 for at least 1 word of data
                    word remainder_size = hdr->w - words - OVERHEAD_WORDS;
                    int remainder_class = get_size_class(remainder_size);

                    // acquire remainder's lock if different
                    bool need_remainder_lock = (remainder_class != i);
                    if (need_remainder_lock) {
                        if (remainder_class < i) {
                            pthread_mutex_unlock(&size_class_locks[i]);
                            pthread_mutex_lock(&size_class_locks[remainder_class]);
                            pthread_mutex_lock(&size_class_locks[i]);
                        } else {
                            pthread_mutex_lock(&size_class_locks[remainder_class]);
                        }
                    }

                    void *mem = split_block(hdr, words);

                    if (need_remainder_lock) {
                        pthread_mutex_unlock(&size_class_locks[remainder_class]);
                    }
                    pthread_mutex_unlock(&size_class_locks[i]);
                    return mem;
                } else {
                    // use whole block
                    set_block_metadata(hdr, hdr->w, true);
                    pthread_mutex_unlock(&size_class_locks[i]);
                    return (void *)((char *)hdr + HEADER_SIZE);
                }
            }
            hdr = offset_to_ptr(hdr->next_offset);
        }

        pthread_mutex_unlock(&size_class_locks[i]);
    }

    // no suitable free block - allocate from new memory
    pthread_mutex_lock(&heap_expand_lock);

    header *hdr = heap_top;

    if (words > MAXWORDS) {
        pthread_mutex_unlock(&heap_expand_lock);
        reterr(err_no_mem);
    }

    void *mem = allocate_from_fresh_memory(words, hdr);

    // Advance heap_top for next allocation
    footer *ftr = GET_FOOTER(hdr);
    heap_top = GET_NEXT_HEADER(ftr);

    pthread_mutex_unlock(&heap_expand_lock);
    return mem;
}

// attempt to coalesce with next block (forward coalescing)
// returns new size class after coalescing (or current if no coalesce)
static int try_coalesce_forward(header *hdr, int current_class) {
    footer *ftr = GET_FOOTER(hdr);
    header *next = GET_NEXT_HEADER(ftr);

    if (next->w == 0 || next->alloced) {
        return current_class;  // can't coalesce
    }

    int next_class = get_size_class(next->w);
    bool need_lock = acquire_neighbor_lock_ordered(current_class, next_class);

    // try to remove neighbor - skip if already being coalesced
    if (!remove_from_free_list_checked(next)) {
        if (need_lock) {
            pthread_mutex_unlock(&size_class_locks[next_class]);
        }
        return current_class;
    }

    // remove current from free list
    remove_from_free_list(hdr);

    // merge blocks
    hdr->w += next->w + OVERHEAD_WORDS;
    ftr = GET_FOOTER(hdr);
    ftr->w = hdr->w;
    ftr->alloced = false;

    // release neighbor's lock
    if (need_lock) {
        pthread_mutex_unlock(&size_class_locks[next_class]);
    }

    // return new size class
    int new_class = get_size_class(hdr->w);

    // switch to new class lock if needed
    if (new_class != current_class) {
        if (new_class < current_class) {
            pthread_mutex_unlock(&size_class_locks[current_class]);
            pthread_mutex_lock(&size_class_locks[new_class]);
        } else {
            pthread_mutex_lock(&size_class_locks[new_class]);
            pthread_mutex_unlock(&size_class_locks[current_class]);
        }
    }

    // add coalesced block back
    add_to_free_list(hdr);
    return new_class;
}

// attempt to coalesce with previous block (backward coalescing)
// returns pointer to merged block and its size class
static header *try_coalesce_backward(header *hdr, int current_class, int *out_class) {
    if ((char *)hdr <= (char *)memspace) {
        *out_class = current_class;
        return hdr;  // at start of heap
    }

    footer *prev_ftr = GET_PREV_FOOTER(hdr);
    if (prev_ftr->w == 0 || prev_ftr->alloced) {
        *out_class = current_class;
        return hdr;  // can't coalesce
    }

    header *prev_hdr = GET_HEADER_FROM_FOOTER(prev_ftr);
    int prev_class = get_size_class(prev_hdr->w);
    bool need_lock = acquire_neighbor_lock_ordered(current_class, prev_class);

    // try to remove neighbor - skip if already being coalesced
    if (!remove_from_free_list_checked(prev_hdr)) {
        if (need_lock) {
            pthread_mutex_unlock(&size_class_locks[prev_class]);
        }
        *out_class = current_class;
        return hdr;
    }

    // remove current from free list
    remove_from_free_list(hdr);

    // merge blocks (prev absorbs current)
    prev_hdr->w += hdr->w + OVERHEAD_WORDS;
    footer *ftr = GET_FOOTER(prev_hdr);
    ftr->w = prev_hdr->w;
    ftr->alloced = false;

    // determine new size class
    int new_class = get_size_class(prev_hdr->w);

    // release old locks
    if (need_lock) {
        pthread_mutex_unlock(&size_class_locks[prev_class]);
    }
    if (current_class != new_class) {
        pthread_mutex_unlock(&size_class_locks[current_class]);
    }

    // acquire new class lock
    if (new_class != prev_class || need_lock) {
        pthread_mutex_lock(&size_class_locks[new_class]);
    }

    // add coalesced block back
    add_to_free_list(prev_hdr);

    *out_class = new_class;
    return prev_hdr;
}

void free(void *ptr) {
    if (ptr == NULL) {
        return;
    }

    header *hdr = (header *)((char *)ptr - HEADER_SIZE);
    int current_class = get_size_class(hdr->w);

    pthread_mutex_lock(&size_class_locks[current_class]);

    // mark as free
    hdr->alloced = false;
    footer *ftr = GET_FOOTER(hdr);
    ftr->alloced = false;

    add_to_free_list(hdr);

    // try forward coalescing
    current_class = try_coalesce_forward(hdr, current_class);

    // try backward coalescing
    hdr = try_coalesce_backward(hdr, current_class, &current_class);

    pthread_mutex_unlock(&size_class_locks[current_class]);
}


void show(header *hdr) {
    if (hdr == NULL) return;

    header *p = hdr;
    int32 n = 1;

    while (p->w != 0) {
        printf("Block %d: %u words, %s at %p\n",
               n, p->w,
               p->alloced ? "allocated" : "free",
               (void *)p);

        footer *ftr = GET_FOOTER(p);
        p = GET_NEXT_HEADER(ftr);
        n++;
    }
}

void init_allocator(void) {
    // initialize heap_top to start of memspace
    heap_top = (header *)memspace;

    // initialize all mutexes
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        pthread_mutex_init(&size_class_locks[i], NULL);
    }
    pthread_mutex_init(&heap_expand_lock, NULL);
}
