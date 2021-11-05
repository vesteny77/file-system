//
// Created by Steven Yuan on 2021-06-14.
//

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void extract_parent_path(char *path, char *buf) {
    char *last = strrchr(path, '/');
    size_t diff = strlen(path) - strlen(last);
    if (diff == 0) {
        strcpy(buf, "/");
        buf[1] = '\0';
    } else {
        strncpy(buf, path, diff);
    }
}

void extract_child_path(char *path, char *buf) {
    char *last = strrchr(path, '/');
    strncpy(buf, last + 1, strlen(last) + 1);
    buf[strlen(last) + 1] = '\0';
}

void test_strtok(char *path) {
    char *token = strtok(path, "/");
    while (token != NULL) {
        printf("%s\n", token);
        token = strtok(NULL, "/");
    }
}

int roundup(double n) {
    if ((n - (double) ((int) n)) != 0.0) {
        return ((int) n) + 1;
    } else {
        return (int) n;
    }
}

int main() {
    printf("Round up 1: %d\n", roundup(1));
    printf("Round up 0.1: %d\n", roundup(0.1));
    printf("Round up 1.2: %d\n", roundup(1.2));
    printf("Round up 2.6: %d\n", roundup(2.6));
    return 0;

}