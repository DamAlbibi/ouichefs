#!/bin/bash

# Init variable de chemin modifiable

DIR_PART1=part1
FIC1=fic1 
FIC2=fic2 
HARD_LINK1=lien1
HARD_LINK2=lien2
DIR_PART2=part2
SYSFS_DIR=/sys/kernel/ouichfs_part

# Creation de 2 partitions differentes

. ../utils/montage_2_part.sh

# Affichage sysfs

echo -e "--------------------------------------------------------\n"

echo -e "\nAffichage du sysfs pour la partition 1 :"
cat "${SYSFS_DIR}/0"

echo -e "\nAffichage du sysfs pour la partition 2 :"
cat "${SYSFS_DIR}/1"

echo -e "--------------------------------------------------------\n"

# Creation de 2 hard link differents dans la partition 1

echo -e "--------------------------------------------------------\n"

echo -e "Creation de deux hard link different sur 2 fichiers differents\n"

cd $DIR_PART1
if ! test -f "$FIC1"; then
        touch $FIC1
fi 

if test -f "$HARD_LINK1"; then
        rm $HARD_LINK1
fi
ln $FIC1 $HARD_LINK1

if ! test -f "$FIC2"; then
        touch $FIC2
fi 

if test -f "$HARD_LINK2"; then
        rm $HARD_LINK2
fi
ln $FIC2 $HARD_LINK2

# De nouveau un affichage du sysfs

if ! test -d "$SYSFS_DIR"; then 
        echo "Repertoire {$SYSFS_DIR} non existant !"
else 
        if ! test -f "${SYSFS_DIR}/0"; then 
                echo "Fichier {$SYSFS_DIR}/0 non present !"
        else 
                echo -e "\nAffichage du sysfs pour la partition 1 :"
                cat "${SYSFS_DIR}/0"
        fi

        if ! test -f "${SYSFS_DIR}/1"; then 
                echo "Fichier {$SYSFS_DIR}/1 non present !"
        else 
                echo -e "\nAffichage du sysfs pour la partition 2 :"
                cat "${SYSFS_DIR}/1"
        fi
fi

echo -e "--------------------------------------------------------\n"