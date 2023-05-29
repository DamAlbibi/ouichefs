#!/bin/bash

# Init variable de chemin modifiable

DIR_PART=part1
DIR_PART2=part2
FILE_FIC=fic
DISTANT_LINK=lien_distant


# Creation de deux partitions

. ../utils/montage_2_part.sh

# Creation lien distant

echo -e "\nCreation d'un lien distant entre part1/fic et part2/lien_distant : \n"

cd $DIR_PART
touch $FILE_FIC
ln -s $FILE_FIC "../${DIR_PART2}/$DISTANT_LINK"

echo "TEST" >> $FILE_FIC

cd ..
echo -n "Contenu fichier fic : "
cat "${DIR_PART}/$FILE_FIC"
echo -n "Contenu fichier lien_distant : "
cat "${DIR_PART2}/$DISTANT_LINK"

