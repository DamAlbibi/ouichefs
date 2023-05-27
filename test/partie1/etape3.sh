#!/bin/bash

# Init variable de chemin modifiable

# Creation de hard link

. ../utils/montage_1_part.sh


# On enleve les liens
unlink part1/lien

# On affiche de nouveau le sysfs 
cat /sys/kernel/ouichfs_part/0