#!/bin/sh
export KERNELDIR=`readlink -f .`
export ARCH=arm
export CROSS_COMPILE=/home/nian/arm-linux-androideabi-4.7/bin/arm-linux-androideabi-
export BUILD_LOCALVERSION="CherryS.Sam-V1.2-Nian"

if [ ! -f $KERNELDIR/.config ];
then
  make defconfig psn_sc03e_v3.5.1_defconfig
fi

. $KERNELDIR/.config


cd $KERNELDIR/
make -j8 || exit 1

mkdir -p $KERNELDIR/BUILT/lib/modules

rm $KERNELDIR/BUILT/lib/modules/*
rm $KERNELDIR/BUILT/zImage

find -name '*.ko' -exec cp -av {} $KERNELDIR/BUILT/lib/modules/ \;
${CROSS_COMPILE}strip --strip-unneeded $KERNELDIR/BUILT/lib/modules/*
cp $KERNELDIR/arch/arm/boot/zImage $KERNELDIR/BUILT/
