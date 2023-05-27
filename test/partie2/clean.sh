#!/bin/bash

DIR_PART1=part1
DIR_PART2=part2

# Demontage des partitions + suppression des repertoire associe s'ils existent

umount $DIR_PART1
umount $DIR_PART2

if test -d "$DIR_PART1"; then
        rm -r $DIR_PART1
fi

if test -d "$DIR_PART2"; then
        rm -r "$DIR_PART2"
fi