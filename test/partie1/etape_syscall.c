#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<sys/syscall.h>
#include<sys/types.h>
int main(int argc, char *argv[]){
    if (argc != 2)
        return 1;

    syscall(335, argv[1]);
    return 0;
}
