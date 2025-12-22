bits 32
global _memspace
%define Heapside (1024*1024*1024/4)

section .data
_memspace:
    times Heapside dd 0