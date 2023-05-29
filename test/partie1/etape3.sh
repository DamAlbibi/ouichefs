#!/bin/bash

# Init variable de chemin modifiable

DIR_PART=part1
FILE_FIC=fic
HARD_LINK=lien
SYSFS_DIR=/sys/kernel/ouichfs_part

# Montage d'une partition

. ../utils/montage_1_part.sh

# Creation de hard link

cd $DIR_PART
if ! test -f "$FILE_FIC"; then
        touch $FILE_FIC
fi 

if test -f "$HARD_LINK"; then
        rm $HARD_LINK
fi
ln $FILE_FIC $HARD_LINK

# On affiche le sysfs pour montrer le nombre de fichier ayant des hard link

cat "${SYSFS_DIR}/0"

# On enleve les liens
unlink $HARD_LINK

# On affiche de nouveau le sysfs pour voir qu'il y en a 1 de moins
cat "${SYSFS_DIR}/0"