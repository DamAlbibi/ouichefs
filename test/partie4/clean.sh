#!/bin/bash

FILE_IMG1=test.img
FILE_IMG2=test2.img
FILE_FIC=fic
DISTANT_LINK=lien_distant
DIR_PART1=part1
DIR_PART2=part2

# Demontage des partitions + suppression des repertoire associe s'ils existent

umount $DIR_PART1
umount $DIR_PART2

if test -f "$DISTANT_LINK"; then
        rm "${DIR_PART2}/$DISTANT_LINK"
fi

if test -f "$FILE_FIC"; then
        rm "${DIR_PART1}/$FILE_FIC"
fi

if test -d "$DIR_PART1"; then
        rm -r $DIR_PART1
fi

if test -d "$DIR_PART2"; then
        rm -r "$DIR_PART2"
fi

if test -f "$FILE_IMG1"; then
        rm $FILE_IMG1
fi

if test -f "$FILE_IMG2"; then
        rm $FILE_IMG2
fi
