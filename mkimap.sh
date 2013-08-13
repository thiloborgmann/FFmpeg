#!/bin/bash

# this script expects input via stdin from "git request-pull"
# it then reformats the output to be accepted by "git imap-send"

# From \
# To: FFmpeg development discussions and patches <ffmpeg-devel@ffmpeg.org>
# From: Thilo Borgmann <thilo.borgmann@mail.de>
# Date: \
# Subject: [FFmpeg-devel] [PATCH] Update my email address.

IFS=""

TO="FFmpeg development discussions and patches <ffmpeg-devel@ffmpeg.org>"
FROM="Thilo Borgmann <thilo.borgmann@mail.de>"
SUBJECT=""

BUFFER=""
NEWLINE="
"

while read -r LINE
do
    BUFFER+="$LINE"$NEWLINE

    if [ "$LINE" = "----------------------------------------------------------------" ]
    then
        read -r SKIP
        read -r COMMIT_1
        BUFFER+="$SKIP"$NEWLINE
        BUFFER+="$COMMIT_1"$NEWLINE
        SUBJECT="[PATCH] $(echo $COMMIT_1 | sed -e 's/^ *//')"
        break;
    fi
done

echo "From "
echo "To:" $TO
echo "From:" $FROM
echo "Date: "
echo "Subject:" $SUBJECT
echo
echo -n "$BUFFER"

while read -r LINE
do
    printf "%s\n" $LINE
done

