//
// Created by Steven Yuan on 2021-06-18.
//

#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(){
    int fd = open("../test.txt", O_RDONLY);
    if (fd == -1) {
        perror("File not found");
        exit(1);
    }
    char buf[256] = {'\0'};
    if (pread(fd, buf, 5, 5) == -1) {
        perror("Write failed:(");
        exit(1);
    }
    fprintf(stdout, "%s", buf);
    close(fd);
    return 0;
}