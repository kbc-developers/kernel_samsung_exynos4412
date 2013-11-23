#!/bin/bash

if [ ! -e ./bmp2splash ]; then
    echo "make bmp2splash..."
    make
fi

echo "generate splash image from $1..."
./bmp2splash $1 > ../../drivers/video/samsung/logo_rgb24_user.h
if [ $? != 0 ]; then
   exit -1
fi


