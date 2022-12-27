//
// Created by pouya on 11/10/22.
//
#include "types.h"
#include "fcntl.h"
#include "user.h"

// simple program to test getpid() system call
// and use gdb for examining how the system call works
void test_getpid(void) {
    write(1, "testing STSCALL getpid()...\n", 28);
    int pid = getpid();
    if (pid == -1) {
        write(1, "getpid() failed!\n", 18);
        exit();
    } else {
        write(1, "getpid() passed!\n", 18);
    }
    printf(1, "pid: %d\n", pid);
    exit();
}
