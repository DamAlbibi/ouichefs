#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "ioctl_interface.h"

int main(int argc, char const *argv[])
{       
    /*struct ouichefs_hard_list lst;
    int dev;
    int fd;
    char c;*/
    int fd;
    int dev;
    
    if((fd = open("/sda2/test.txt", O_RDWR)) < 0) {
        perror("open ouichefs");
        exit(1);
    }
    
    if((dev = open("/dev/ouichefs", O_RDWR)) < 0) {
        perror("open ouichefs");
        exit(1);
    }
    
    
    

    if (ioctl(dev, OUICHEFSG_HARD_LIST, &fd) == -1) {
        perror("ioctl");
        exit(1);
    }
    // Iteration sur chaque path par hardlink

    /*
    char *buf;
    char chunk[MAX_LEN];
    int nbr_max_link = MAX_LINK;
    int i = 0;
    for (int i = 0; i < MAX_LINK; i++) {
        memcpy(chunk, buf, MAX_LEN);
        printf("Path = %s\n", chunk);
    }
    close(fd);
    return 0;
    */
}
