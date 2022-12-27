//
// Created by pouya on 11/11/22.
//
#include "types.h"
#include "fcntl.h"
#include "user.h"

// simple program to test find_largest_prime_factor() system call
int main(int argc, char *argv[]) {
    write(1, "testing find_largest_prime_factor system call...\n", 49);
    if (argc != 2) {
        printf(2, "Error in syntax; please call like:\n>> test_bpf <number>\n");
        exit();
    }
    int n = atoi(argv[1]), prev_ebx;
    asm volatile(
            "movl %%ebx, %0;"
            "movl %1, %%ebx;"
            : "=r" (prev_ebx)
            : "r"(n)
            );
    printf(1, "calling find_largest_prime_factor(%d)...\n", n);
    int result = find_largest_prime_factor();
    asm volatile(
            "movl %0, %%ebx;"
            : : "r"(prev_ebx)
            );
    if (result == -1) {
        write(1, "find_largest_prime_factor () failed!\n", 37);
        write(1, "please check i you entered an integer bigger than 1\n", 52);
        exit();
    }
    printf(1, "find_largest_prime_factor(%d) = %d\n", n, result);
    exit();
}
