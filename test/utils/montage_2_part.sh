#!/bin/bash

# Init variable de chemin modifiable

FILE_IMG=test.img
FILE_IMG2=test2.img
DIR_PART1=part1
DIR_PART2=part2

# Creation de 2 partitions differentes

echo -e "--------------------------------------------------------\n"

echo -e "Montage des 2 partitions part1 et part2 :\n"

if ! test -f "$FILE_IMG"; then
        dd if=/dev/zero of=$FILE_IMG bs=1M count=50
fi

if ! test -f "$FILE_IMG2"; then
        dd if=/dev/zero of=$FILE_IMG2 bs=1M count=50
fi

if ! test -d "$DIR_PART1"; then
        mkdir $DIR_PART1
fi 

/usr/bin/mkfs.ouichefs $FILE_IMG
mount $FILE_IMG $DIR_PART1

if ! test -d "$DIR_PART2"; then
         mkdir $DIR_PART2
fi 

/usr/bin/mkfs.ouichefs $FILE_IMG2
mount $FILE_IMG2 $DIR_PART2

echo -e "--------------------------------------------------------\n"