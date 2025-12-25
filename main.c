#include "alloc.h"
#include <stdio.h>

int main() {
    init_allocator();
    char *p1 = alloc(40);   // 10 words
    char *p2 = alloc(80);   // 20 words
    char *p3 = alloc(120);  // 30 words
    char *p4 = alloc(160);  // 40 words
    
    printf("=== Initial ===\n");
    show((header *)memspace);
    
    printf("\n=== Free p1, p2, p3 (should coalesce) ===\n");
    dealloc(p1);
    dealloc(p2);
    dealloc(p3);
    
    show((header *)memspace);

    
    return 0;
}