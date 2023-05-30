#!/bin/bash

# Init variable de chemin modifiable

FILE_IMG=test.img
FILE_IMG2=test2.img
DIR_PART1=part1
DIR_PART2=part2
SYSFS_DIR=/sys/kernel/ouichfs_part

# Creation de 2 partitions differentes

. ../utils/montage_2_part.sh

# Affichage du sysfs pour voir leur uuid 

echo -e "--------------------------------------------------------\n"

if ! test -d "$SYSFS_DIR"; then 
        echo "Repertoire {$SYSFS_DIR} non existant !\n"
fi

if ! test -f "${SYSFS_DIR}/0"; then 
        echo "Fichier {$SYSFS_DIR}/0 non present !\n"
else 
        echo -e "Affichage du sysfs pour la partition 1 :\n"
        cat "${SYSFS_DIR}/0"
fi

echo -e "--------------------------------------------------------\n"

echo -e "--------------------------------------------------------\n"

if ! test -f "${SYSFS_DIR}/1"; then 
        echo "Fichier {$SYSFS_DIR}/1 non present !\n"
else 
        echo -e "Affichage du sysfs pour la partition 2 :\n"
        cat "${SYSFS_DIR}/1"
fi

echo -e "--------------------------------------------------------\n"