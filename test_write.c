//
// Created by Steven Yuan on 2021-06-18.
//
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(){
    int fd = open("../test.txt", O_CREAT | O_RDWR);
    if (pwrite(fd, "hello", 5, 10) == -1) {
        perror("Write failed:(");
        exit(1);
    }
    close(fd);
    return 0;
}