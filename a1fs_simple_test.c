//
// Created by Steven Yuan on 2021-05-24.
//

#include <stdio.h>
#include "a1fs.h"

int main(void) {
    printf("The size of a1fs_inode is %lu\n", sizeof(a1fs_inode));
    printf("The size of a1fs_dentry is %lu\n", sizeof(a1fs_dentry));
    printf("The size of a1fs_extent is %lu\n", sizeof(a1fs_extent));
}