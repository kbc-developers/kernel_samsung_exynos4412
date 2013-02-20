#!/bin/bash

if [ ! -e ./release-tools/bmp2splash/bmp2splash ]; then
    echo "make bmp2splash..."
    make -C ./release-tools/bmp2splash
fi

echo "generate splash image from $1..."
./release-tools/bmp2splash/bmp2splash $1 > ./drivers/video/samsung/logo_rgb24_user.h

if [ $? != 0 ]; then
   exit -1
fi
