#!/bin/bash

#find all wrong licensed files that are of a certain type
TYPES="c cpp h S asm v texi"

echo $TYPES

for TYPE in $TYPES
do
    FILES+="`fgrep -Rl --include '*.'"$TYPE" 'This file is part of Libav' *` "
done


# replace all wrong mentioning of libav
TEMP=`mktemp XXXXXX`

for FILE in $FILES
do
    echo $FILE...
    cat $FILE | sed -e 's/[[:<:]]Libav[[:>:]]/FFmpeg/g' > $TEMP
    mv $TEMP $FILE
done
