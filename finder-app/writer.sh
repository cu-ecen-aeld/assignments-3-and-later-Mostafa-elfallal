#!/bin/bash

if [ "$#" -ne 2 ];then
        echo "Number of parameters is not 2"
        exit 1
fi

install -D /dev/null $1

echo $2 > $1

exit $?
