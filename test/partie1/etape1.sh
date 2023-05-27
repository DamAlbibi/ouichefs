#!/bin/bash

# Init variable de chemin modifiable

DIR_PART=part1
FILE_FIC=fic
HARD_LINK=lien

# Creation 1 partition

. ../utils/montage_1_part.sh

sleep 1

# Creation hard link (ETAPE 1)
echo -e "\nCreation hard link + lecture :"

cd $DIR_PART
if ! test -f "$FILE_FIC"; then
        touch $FILE_FIC
fi 

if test -f "$HARD_LINK"; then
        rm $HARD_LINK
fi
ln $FILE_FIC $HARD_LINK
echo "POUF" >> $FILE_FIC
echo -n "Contenu fichier fic : "
cat $FILE_FIC 
echo -n "Contenu fichier lien : "
cat $HARD_LINK 

# Creation de multiple fichier sans hard link 
# Afin de tester l'affichage du sysfs sur le nombre total 
