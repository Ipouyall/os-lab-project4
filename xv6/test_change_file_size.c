//
// Created by pouya on 11/13/22.
//
#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[]) {
    if (argc != 3) {
        printf(1, "Usage: test_change_file_size <file_name> <new_size>\n");
        exit();
    }
    change_file_size(argv[1], atoi(argv[2]));
    exit();
}