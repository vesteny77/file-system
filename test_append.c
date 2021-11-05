//
// Created by Steven Yuan on 2021-06-18.
//

#include <stdio.h>

int main(){
    FILE *fp;
    fp = fopen("../test.txt", "a");
    fprintf(fp, "hello");
    fclose(fp);
}