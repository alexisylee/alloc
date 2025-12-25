#include "alloc.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

// helper to get header from pointer
header *get_header(void *ptr) {
    return (header *)((char *)ptr - HEADER_SIZE);
}


void test_multiple_allocations() {
    memset(memspace, 0, 1024 * 1024 * 1024);
    init_allocator();
    char *p1 = alloc(40);
    char *p2 = alloc(80);
    char *p3 = alloc(120);

    assert(p1 != NULL && p2 != NULL && p3 != NULL);
    assert(p2 > p1 && p3 > p2);

    // diff should be 40 bytes data + HEADER_SIZE + FOOTER_SIZE
    ptrdiff_t diff = (char *)p2 - (char *)p1;
    ptrdiff_t expected_diff = 40 + HEADER_SIZE + FOOTER_SIZE;
    assert(diff == expected_diff);
}

void test_free_and_reuse() {
    memset(memspace, 0, 1024 * 1024 * 1024);
    init_allocator();
    char *p1 = alloc(40);
    char *p2 = alloc(80);
    char *p3 = alloc(120);
    (void) p1;
    (void) p3;
    dealloc(p2);

    header *h2 = get_header(p2);
    assert(atomic_load(&h2->alloced) == false);

    char *p4 = alloc(60);
    assert(p4 == p2);
    assert(atomic_load(&h2->alloced) == true);
}

void test_forward_coalesce() {
    memset(memspace, 0, 1024 * 1024 * 1024);
    init_allocator();
    char *p1 = alloc(40);
    char *p2 = alloc(80);
    char *p3 = alloc(120);
    (void) p1;
    dealloc(p2);
    dealloc(p3);

    header *h2 = get_header(p2);
    assert(atomic_load(&h2->w) == 55);
    assert(atomic_load(&h2->alloced) == false);
}

void test_backward_coalesce() {
    memset(memspace, 0, 1024 * 1024 * 1024);
    init_allocator();
    char *p1 = alloc(40);
    char *p2 = alloc(80);
    char *p3 = alloc(120);
    (void) p3;

    dealloc(p2);
    dealloc(p1);

    header *h1 = get_header(p1);
    assert(atomic_load(&h1->w) == 35);
    assert(atomic_load(&h1->alloced) == false);
}

void test_full_coalesce() {
    memset(memspace, 0, 1024 * 1024 * 1024);
    init_allocator();
    char *p1 = alloc(40);
    char *p2 = alloc(80);
    char *p3 = alloc(120);
    char *p4 = alloc(160);
    (void) p4;

    dealloc(p1);
    dealloc(p2);
    dealloc(p3);

    header *h1 = get_header(p1);
    assert(atomic_load(&h1->w) == 70);
    assert(atomic_load(&h1->alloced) == false);
}

void test_write_read() {
    memset(memspace, 0, 1024 * 1024 * 1024);
    init_allocator();
    char *p = alloc(20);
    
    strcpy((char *)p, "Hello, Allocator!");
    assert(strcmp((char *)p, "Hello, Allocator!") == 0);
}

void test_splitting() {
    memset(memspace, 0, 1024 * 1024 * 1024);
    init_allocator();

    char *p1 = alloc(400);
    dealloc(p1);
    // show(memspace);

    char *p2 = alloc(40);
    // show(memspace);
    header *h2 = get_header(p2);
    assert(atomic_load(&h2->w) == 10);

    footer *ftr = GET_FOOTER(h2);
    header *remainder = GET_NEXT_HEADER(ftr);
    printf("%d\n", atomic_load(&remainder->w));
    // Calculation: 100 (original) - 10 (allocated) - overhead words
    assert(atomic_load(&remainder->w) == 85);
    assert(atomic_load(&remainder->alloced) == false);
}

void test_free_null() {
    dealloc(NULL);
    assert(1);  // just checking it doesn't crash
}

void test_footer_consistency() {
    memset(memspace, 0, 1024 * 1024 * 1024);
    init_allocator();
    char *p = alloc(80);

    header *h = get_header(p);
    footer *f = GET_FOOTER(h);

    assert(atomic_load(&h->w) == atomic_load(&f->w));
    assert(atomic_load(&h->alloced) == atomic_load(&f->alloced));
}

void test_stress_sequential() {
    memset(memspace, 0, 1024 * 1024 * 1024);

    const int NUM_ALLOCS = 1000000;
    void *ptrs[1000];
    int ptr_idx = 0;

    for (int i = 0; i < NUM_ALLOCS; i++) {
        // Vary allocation sizes
        int size = (i % 500) + 16;
        void *p = alloc(size);
        assert(p != NULL);

        // Write to memory to ensure it's valid
        ((char *)p)[0] = 'A';

        // Store some pointers to free later
        if (ptr_idx < 1000) {
            ptrs[ptr_idx++] = p;
        } else {
            // Free half, keep half
            if (i % 2 == 0) {
                dealloc(p);
            } else {
                ptrs[i % 1000] = p;
            }
        }

        // Periodically free accumulated blocks
        if (i % 10000 == 0 && i > 0) {
            for (int j = 0; j < 500; j++) {
                if (ptrs[j]) {
                    dealloc(ptrs[j]);
                    ptrs[j] = NULL;
                }
            }
        }
    }

    // Cleanup
    for (int i = 0; i < 1000; i++) {
        if (ptrs[i]) {
            dealloc(ptrs[i]);
        }
    }

    printf("Sequential stress test passed\n");
}

void test_stress_fragmentation() {
    memset(memspace, 0, 1024 * 1024 * 1024);

    void *ptrs[10000];

    // Allocate many blocks of varying sizes
    for (int i = 0; i < 10000; i++) {
        int size = ((i * 7) % 1000) + 16;
        ptrs[i] = alloc(size);
        assert(ptrs[i] != NULL);
    }

    // Free every other block to create fragmentation
    for (int i = 0; i < 10000; i += 2) {
        dealloc(ptrs[i]);
        ptrs[i] = NULL;
    }

    // Try to allocate and fill the gaps
    for (int i = 0; i < 5000; i++) {
        int size = ((i * 11) % 500) + 16;
        void *p = alloc(size);
        assert(p != NULL);
        dealloc(p);
    }

    // Cleanup
    for (int i = 1; i < 10000; i += 2) {
        dealloc(ptrs[i]);
    }

    printf("Fragmentation stress test passed\n");
}

// int main() {
//     test_multiple_allocations();
//     test_free_and_reuse();
//     test_forward_coalesce();
//     test_backward_coalesce();
//     test_full_coalesce();
//     test_write_read();
//     test_splitting();
//     test_free_null();
//     test_footer_consistency();
//     test_stress_sequential();
//     test_stress_fragmentation();
//     return 0;
// }

void *worker_basic(void *arg) {
    long tid = (long)arg;

    for (int i = 0; i < 10; i++) {
        void *p = alloc(100);
        printf("Thread %ld allocated %p\n", tid, p);

        if (p) {
            ((char *)p)[0] = 'X';
            dealloc(p);
            printf("Thread %ld freed %p\n", tid, p);
        }
    }
    return NULL;
}

void *worker_stress(void *arg) {
    long tid = (long)arg;
    void *ptrs[100] = {NULL};

    for (int i = 0; i < 10000; i++) {
        // Vary allocation sizes across different size classes
        int size = ((tid * 1000 + i) % 600) + 16;
        void *p = alloc(size);

        if (p) {
            // Write to memory
            ((char *)p)[0] = (char)('A' + tid);

            // Store for later freeing
            int idx = i % 100;
            if (ptrs[idx]) {
                dealloc(ptrs[idx]);
            }
            ptrs[idx] = p;
        }
    }

    // Cleanup
    for (int i = 0; i < 100; i++) {
        if (ptrs[i]) {
            dealloc(ptrs[i]);
        }
    }

    return NULL;
}

void *worker_mixed_sizes(void *arg) {
    long tid = (long)arg;

    for (int i = 0; i < 5000; i++) {
        // Create allocations that span multiple size classes
        int size_class = i % 8;
        int size;

        switch (size_class) {
            case 0: size = 20;    break;  // class 0
            case 1: size = 50;    break;  // class 1
            case 2: size = 100;   break;  // class 2
            case 3: size = 200;   break;  // class 3
            case 4: size = 400;   break;  // class 4
            case 5: size = 800;   break;  // class 5
            case 6: size = 1600;  break;  // class 6
            case 7: size = 3200;  break;  // class 7
            default: size = 100;  break;
        }

        void *p = alloc(size);
        if (p) {
            ((char *)p)[0] = (char)tid;
            ((char *)p)[size - 1] = (char)tid;

            // Immediately free some, keep others
            if (i % 3 == 0) {
                dealloc(p);
            } else if (i % 3 == 1) {
                // Free after a short "use"
                for (int j = 0; j < size; j += 100) {
                    ((char *)p)[j] = 'X';
                }
                dealloc(p);
            }
        }
    }

    return NULL;
}

void test_concurrent_basic() {
    printf("Running basic concurrent test (4 threads)...\n");
    memset(memspace, 0, 1024 * 1024 * 1024);
    init_allocator();

    pthread_t threads[4];
    for (int i = 0; i < 4; i++) {
        pthread_create(&threads[i], NULL, worker_basic, (void *)(long)i);
    }

    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("Basic concurrent test passed\n");
}

void test_concurrent_stress() {
    printf("Running concurrent stress test (8 threads, 10k ops each)...\n");
    memset(memspace, 0, 1024 * 1024 * 1024);
    init_allocator();

    pthread_t threads[8];
    for (int i = 0; i < 8; i++) {
        pthread_create(&threads[i], NULL, worker_stress, (void *)(long)i);
    }

    for (int i = 0; i < 8; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("Concurrent stress test passed\n");
}

void test_concurrent_mixed_sizes() {
    printf("Running concurrent mixed sizes test (16 threads)...\n");
    memset(memspace, 0, 1024 * 1024 * 1024);
    init_allocator();

    pthread_t threads[16];
    for (int i = 0; i < 16; i++) {
        pthread_create(&threads[i], NULL, worker_mixed_sizes, (void *)(long)i);
    }

    for (int i = 0; i < 16; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("Concurrent mixed sizes test passed\n");
}

int main(int argc, char *argv[]) {
    const char *test_mode = (argc > 1) ? argv[1] : "basic";

    if (strcmp(test_mode, "unit") == 0) {
        printf("Running unit tests\n");
        test_multiple_allocations();
        test_free_and_reuse();
        test_forward_coalesce();
        test_backward_coalesce();
        test_full_coalesce();
        test_write_read();
        test_splitting();
        test_free_null();
        test_footer_consistency();

        printf("All unit tests passed\n");

    } else if (strcmp(test_mode, "stress") == 0) {
        printf("Running stress tests\n");
        test_stress_sequential();
        test_stress_fragmentation();
        printf("All stress tests passed\n");

    } else if (strcmp(test_mode, "concurrent") == 0) {
        printf("Running concurrent tests\n");
        test_concurrent_basic();
        test_concurrent_stress();
        test_concurrent_mixed_sizes();
        printf("All concurrent tests passed\n");

    } else if (strcmp(test_mode, "all") == 0) {

        printf("Running unit tests\n");
        test_multiple_allocations();
        test_free_and_reuse();
        test_forward_coalesce();
        test_backward_coalesce();
        test_full_coalesce();
        test_write_read();
        test_splitting();
        test_free_null();
        test_footer_consistency();
        printf("All unit tests passed\n\n");

        // printf("Running stress tests\n");
        // test_stress_sequential();
        // printf("0");
        // test_stress_fragmentation();
        // printf("All stress tests passed\n\n");

        printf("Running concurrent tests\n");
        test_concurrent_basic();
        printf("1");
        test_concurrent_stress();
        printf("2");
        test_concurrent_mixed_sizes();
        printf("All concurrent tests passed\n\n");

    } else {
        // Default: basic concurrent test
        test_concurrent_basic();
    }

    return 0;
}