//
// Created by ata on 12/11/22.
//
#include "types.h"
#include "fcntl.h"
#include "user.h"
#include "syscall.h"

void call_tests(int sys_call) {
    printf(1, "calling get_callers for %d\n", sys_call);
    get_callers(sys_call);
}

// simple program to test get_callers() system call
int main(int argc, char *argv[]) {
    printf(1, "testing get_callers system call\n");

    call_tests(SYS_fork);
    call_tests(SYS_wait);
    call_tests(SYS_write);

    exit();
}