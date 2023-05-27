#!/bin/bash

FILE_IMG=test.img

if ! test -f "$FILE_IMG"; then 
        dd if=/dev/zero of=$FILE_IMG bs=1M count=50
fi

# montage de la partition
echo -e "Montage de la partition dans part1 :"

if ! test -d "$DIR_PART"; then
        mkdir $DIR_PART
fi 

../../mkfs/./mkfs.ouichefs $FILE_IMG
mount $FILE_IMG $DIR_PART