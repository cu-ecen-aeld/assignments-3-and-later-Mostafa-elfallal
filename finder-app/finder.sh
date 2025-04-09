#!/bin/bash

if [ "$#" -ne 2 ];then
	echo "Number of parameters is not 2"
	exit 1
fi

if [ ! -d  "$1" ];then
	echo "filedir doesn't exist or isn't a directory"
	exit 1
fi

FILE_COUNT=$(find "$1" -type f | wc -l)

MATCHES=$(grep -r "$2" "$1" | wc -l)

echo "The number of files are "$FILE_COUNT" and the number of matching lines are "$MATCHES""


