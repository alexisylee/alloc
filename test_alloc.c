#include "alloc.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

// helper to get header from pointer
header *get_header(void *ptr) {
    return (header *)((char *)ptr - 8);
}


void test_multiple_allocations() {
    memset(memspace, 0, 1024 * 1024 * 1024);
    int8 *p1 = alloc(40);   
    int8 *p2 = alloc(80);   
    int8 *p3 = alloc(120); 
    
    assert(p1 != NULL && p2 != NULL && p3 != NULL);
    assert(p2 > p1 && p3 > p2);
    
    ptrdiff_t diff = (char *)p2 - (char *)p1;
    assert(diff == 52);
}

void test_free_and_reuse() {
    memset(memspace, 0, 1024 * 1024 * 1024);
    int8 *p1 = alloc(40);
    int8 *p2 = alloc(80);
    int8 *p3 = alloc(120);
    (void) p1;
    (void) p3;
    free(p2);
    
    header *h2 = get_header(p2);
    assert(h2->alloced == false);
    
    int8 *p4 = alloc(60);
    assert(p4 == p2);
    assert(h2->alloced == true);
}

void test_forward_coalesce() {
    memset(memspace, 0, 1024 * 1024 * 1024);
    int8 *p1 = alloc(40);   
    int8 *p2 = alloc(80);   
    int8 *p3 = alloc(120);  
    (void) p1;
    free(p2);
    free(p3);
    
    header *h2 = get_header(p2);
    // printf("h2->w is %d\n", h2->w);
    assert(h2->w == 53);
    assert(h2->alloced == false);
}

void test_backward_coalesce() {
    memset(memspace, 0, 1024 * 1024 * 1024);
    int8 *p1 = alloc(40);   
    int8 *p2 = alloc(80);   
    int8 *p3 = alloc(120);  
    (void) p3;
    
    free(p2);
    free(p1);
    
    header *h1 = get_header(p1);
    // printf("h1->w is %d\n", h1->w);

    assert(h1->w == 33);
    assert(h1->alloced == false);
}

void test_full_coalesce() {
    memset(memspace, 0, 1024 * 1024 * 1024);
    int8 *p1 = alloc(40);   
    int8 *p2 = alloc(80);   
    int8 *p3 = alloc(120);  
    int8 *p4 = alloc(160);  
    (void) p4;
    // show((header *)memspace);
    
    free(p1);
    free(p2);
    free(p3);
    // show((header *)memspace);
    
    header *h1 = get_header(p1);
    // printf("%d\n", h1->w);
    assert(h1->w == 66);
    assert(h1->alloced == false);
}

void test_write_read() {
    memset(memspace, 0, 1024 * 1024 * 1024);
    int8 *p = alloc(20);
    
    strcpy((char *)p, "Hello, Allocator!");
    assert(strcmp((char *)p, "Hello, Allocator!") == 0);
}

void test_splitting() {
    memset(memspace, 0, 1024 * 1024 * 1024);

    int8 *p1 = alloc(400);  
    free(p1);
    // show(memspace);
    
    int8 *p2 = alloc(40);
    // show(memspace);
    header *h2 = get_header(p2);
    assert(h2->w == 10);
    
    footer *ftr = GET_FOOTER(h2);
    header *remainder = GET_NEXT_HEADER(ftr);
    // printf("%d\n", remainder->w);
    assert(remainder->w == 88);
    assert(remainder->alloced == false);
}

void test_free_null() {
    free(NULL);
    assert(1);  // just checking it doesn't crash
}

void test_footer_consistency() {
    memset(memspace, 0, 1024 * 1024 * 1024);
    int8 *p = alloc(80);
    
    header *h = get_header(p);
    footer *f = GET_FOOTER(h);
    
    assert(h->w == f->w);
    assert(h->alloced == f->alloced);
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
    
//     return 0;
// }

void *worker(void *arg) {
    long tid = (long)arg;
    
    for (int i = 0; i < 10; i++) { 
        void *p = alloc(100);
        printf("Thread %ld allocated %p\n", tid, p);
        
        if (p) {
            ((char *)p)[0] = 'X';
            free(p);
            printf("Thread %ld freed %p\n", tid, p);
        }
    }
    return NULL;
}

int main() {
    init_allocator();  // Initialize mutexes

    pthread_t threads[4];
    for (int i = 0; i < 4; i++) {
        pthread_create(&threads[i], NULL, worker, (void *)(long)i);
    }
    
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }
    
    return 0;
}