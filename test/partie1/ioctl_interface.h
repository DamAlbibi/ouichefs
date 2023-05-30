// SPDX-License-Identifier: GPL-2.0
#ifndef IOCTL_INTERFACE_H
#define IOCTL_INTERFACE_H

#define MAX_LINK 5
#define MAX_LEN 50
#define MAGIC_IOCTL 'F'

/*struct ouichefs_hard_list {
    int fd; //fd of the file
    int pid; //pid of the process
    char *buff; //MAX_LINK * MAX_PATHLEN
};*/

#define OUICHEFSG_HARD_LIST _IOWR(MAGIC_IOCTL, 0, int *)

#endif
