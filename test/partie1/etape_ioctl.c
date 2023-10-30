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
    int fd;
    int dev;

    if (argc != 3)
        return 1;
    
    if((fd = open(argv[1], O_RDWR)) < 0) {
        perror("open ouichefs");
        exit(1);
    }
    
    if((dev = open(argv[2], O_RDWR)) < 0) {
        perror("open ouichefs");
        exit(1);
    }
    
    
    

    if (ioctl(dev, OUICHEFSG_HARD_LIST, &fd) == -1) {
        perror("ioctl");
        exit(1);
    }
}