#include "alloc.h"

// global state
const word SIZE_CLASS_LIMITS[NUM_SIZE_CLASSES] = {
    8, 16, 32, 64, 128, 256, 512, ~0u
};

// segregated free lists - one per size class
header *free_lists[NUM_SIZE_CLASSES] = {NULL};

// per-size-class mutexes for fine-grained locking
pthread_mutex_t size_class_locks[NUM_SIZE_CLASSES] = {
    PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER
};

// mutex for heap expansion
pthread_mutex_t heap_expand_lock = PTHREAD_MUTEX_INITIALIZER;

// track end of allocated heap
static header *heap_top = NULL;

// thread-local caches, one cache per size class per thread
// each cache is a simple stack of free blocks
typedef struct {
    header *blocks[THREAD_CACHE_SIZE];
    int count;
} thread_cache_t;

__thread thread_cache_t thread_caches[NUM_SIZE_CLASSES] = {0};

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

// remove block from free list - returns true if found
static bool remove_from_free_list_checked(header *hdr) {
    int class = get_size_class(hdr->w);

    if (free_lists[class] == hdr) {
        free_lists[class] = offset_to_ptr(hdr->next_offset);
        hdr->next_offset = 0;
        return true;
    }

    header *curr = free_lists[class];
    while (curr) {
        if (offset_to_ptr(curr->next_offset) == hdr) {
            curr->next_offset = hdr->next_offset;
            hdr->next_offset = 0;
            return true;
        }
        curr = offset_to_ptr(curr->next_offset);
    }

    return false;
}

// initialize block header and footer
static void set_block_metadata(header *hdr, word size, bool is_alloced) {
    hdr->w = size;
    hdr->alloced = is_alloced;

    footer *ftr = GET_FOOTER(hdr);
    ftr->w = size;
    ftr->alloced = is_alloced;
}

// allocate a new block from uninitialized memory
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
static void *split_block(header *hdr, word requested_words) {
    word old_size = hdr->w;

    // set up allocated block
    set_block_metadata(hdr, requested_words, true);
    void *ret = (void *)((char *)hdr + HEADER_SIZE);

    // create remainder block
    footer *ftr = GET_FOOTER(hdr);
    header *remainder = (header *)((char *)ftr + FOOTER_SIZE);
    set_block_metadata(remainder, old_size - requested_words - OVERHEAD_WORDS, false);

    // add remainder to free list
    add_to_free_list(remainder);

    return ret;
}

// refill thread cache from global free list
// returns true if successful, false if global list is empty
static bool refill_thread_cache(int size_class) {
    pthread_mutex_lock(&size_class_locks[size_class]);
    
    thread_cache_t *cache = &thread_caches[size_class];
    int refill_count = THREAD_CACHE_SIZE / 2;  // refill to half capacity
    
    for (int i = 0; i < refill_count && free_lists[size_class] != NULL; i++) {
        header *hdr = free_lists[size_class];
        free_lists[size_class] = offset_to_ptr(hdr->next_offset);
        hdr->next_offset = 0;
        
        cache->blocks[cache->count++] = hdr;
    }
    
    pthread_mutex_unlock(&size_class_locks[size_class]);
    
    return cache->count > 0;
}

// flush some blocks from thread cache back to global free list
static void flush_thread_cache(int size_class) {
    thread_cache_t *cache = &thread_caches[size_class];
    
    if (cache->count == 0) return;
    
    pthread_mutex_lock(&size_class_locks[size_class]);
    
    // flush half the cache
    int flush_count = cache->count / 2;
    for (int i = 0; i < flush_count; i++) {
        header *hdr = cache->blocks[--cache->count];
        hdr->next_offset = ptr_to_offset(free_lists[size_class]);
        free_lists[size_class] = hdr;
    }
    
    pthread_mutex_unlock(&size_class_locks[size_class]);
}

void *alloc(int32 bytes) {
    word words = BYTES_TO_WORDS(bytes);
    int target_class = get_size_class(words);

    // trying thread-local cache first
    thread_cache_t *cache = &thread_caches[target_class];
    if (cache->count > 0) {
        header *hdr = cache->blocks[--cache->count];
        hdr->alloced = true;
        footer *ftr = GET_FOOTER(hdr);
        ftr->alloced = true;
        return (void *)((char *)hdr + HEADER_SIZE);
    }

    // cache miss - try to refill from global free lists
    if (refill_thread_cache(target_class)) {
        // successfully refilled, try again
        header *hdr = cache->blocks[--cache->count];
        hdr->alloced = true;
        footer *ftr = GET_FOOTER(hdr);
        ftr->alloced = true;
        return (void *)((char *)hdr + HEADER_SIZE);
    }

    // no blocks available in this size class, try larger size classes
    for (int i = target_class + 1; i < NUM_SIZE_CLASSES; i++) {
        pthread_mutex_lock(&size_class_locks[i]);

        header *hdr = free_lists[i];
        if (hdr) {
            word hdr_size = hdr->w;
            remove_from_free_list_checked(hdr);

            // check if we should split
            if (hdr_size >= words + OVERHEAD_WORDS + 1) {
                word remainder_size = hdr_size - words - OVERHEAD_WORDS;
                int remainder_class = get_size_class(remainder_size);

                // only split if remainder stays in same class
                if (remainder_class == i) {
                    void *mem = split_block(hdr, words);
                    pthread_mutex_unlock(&size_class_locks[i]);
                    return mem;
                }
            }
            
            // use whole block
            set_block_metadata(hdr, hdr_size, true);
            pthread_mutex_unlock(&size_class_locks[i]);
            return (void *)((char *)hdr + HEADER_SIZE);
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

    // advance heap_top for next allocation
    footer *ftr = GET_FOOTER(hdr);
    heap_top = GET_NEXT_HEADER(ftr);

    pthread_mutex_unlock(&heap_expand_lock);
    return mem;
}

void dealloc(void *ptr) {
    if (ptr == NULL) return;

    header *hdr = (header *)((char *)ptr - HEADER_SIZE);
    word size = hdr->w;
    int size_class = get_size_class(size);

    // mark as free
    hdr->alloced = false;
    footer *ftr = GET_FOOTER(hdr);
    ftr->alloced = false;

    // return to thread-local cache (lockless)
    thread_cache_t *cache = &thread_caches[size_class];
    if (cache->count < THREAD_CACHE_SIZE) {
        cache->blocks[cache->count++] = hdr;
        return;
    }

    // cache full - flush half to global list, then add this block
    flush_thread_cache(size_class);
    cache->blocks[cache->count++] = hdr;
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
    heap_top = (header *)memspace;

    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        free_lists[i] = NULL;
    }
    
    // Thread-local caches are initialized automatically to zero
}
