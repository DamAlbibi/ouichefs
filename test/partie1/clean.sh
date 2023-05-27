#!/bin/bash

FILE_IMG1=test.img
FILE_IMG2=test2.img
DIR_PART1=part1
DIR_PART2=part2
FIC1=fic
FIC2=fic1
FIC3=fic2
HARD_LINK1=lien
HARD_LINK2=lien1
HARD_LINK3=lien2

# Demontage de la partition 

if test -d "$DIR_PART1"; then
        cd $DIR_PART1
        if test -f "$HARDLINK1"; then
                rm $HARD_LINK1
        fi

        if test -f "$HARDLINK2"; then
                rm $HARD_LINK1
        fi

        if test -f "$HARDLINK3"; then
                rm $HARD_LINK1
        fi

        if test -f "$FIC1"; then
                rm $FIC1
        fi

        if test -f "$FIC2"; then
                rm $FIC2
        fi

        if test -f "$FIC3"; then
                rm $FIC3
        fi

        cd ..
fi

umount $DIR_PART1
umount $DIR_PART2

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
