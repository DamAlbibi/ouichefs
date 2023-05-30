#!/bin/bash

# Init variable de chemin modifiable

DIR_PART=part1
DIR_PART2=part2
FILE_FIC=fic
DISTANT_LINK=lien_distant


# Creation de deux partitions

. ../utils/montage_2_part.sh

# Creation lien distant

echo -e "\n--------------------------------------------------------\n"

echo -e "Creation d'un lien distant entre part1/fic et part2/lien_distant : \n"

cd $DIR_PART
touch $FILE_FIC
ln -s $FILE_FIC "../${DIR_PART2}/$DISTANT_LINK"

echo "TEST" >> $FILE_FIC

cd ..
echo -n "Contenu fichier fic : "
cat "${DIR_PART}/$FILE_FIC"

rm "${DIR_PART}/${FILE_FIC}"

echo -n "Contenu fichier lien distant apres migration : "
cat "${DIR_PART2}/$DISTANT_LINK"
stat ${DIR_PART2}/$DISTANT_LINK

echo -e "\n--------------------------------------------------------\n"
