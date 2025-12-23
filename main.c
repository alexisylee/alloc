#include "alloc.h"
#include <stdio.h>

int main() {
    
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