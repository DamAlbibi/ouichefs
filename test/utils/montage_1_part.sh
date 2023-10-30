#!/bin/bash

FILE_IMG=test.img
DIR_PART=part1

echo -e "--------------------------------------------------------\n"

# montage de la partition
echo -e "Montage de la partition dans part1 :\n"

if ! test -f "$FILE_IMG"; then 
        dd if=/dev/zero of=$FILE_IMG bs=1M count=50
fi

if ! test -d "$DIR_PART"; then
        mkdir $DIR_PART
fi 

/usr/bin/mkfs.ouichefs $FILE_IMG
mount $FILE_IMG $DIR_PART

echo -e "--------------------------------------------------------\n"