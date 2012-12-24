#!/bin/bash

export ARCH=arm
export CROSS_COMPILE=/opt/toolchains/arm-eabi-4.4.3/bin/arm-eabi-

make t0_04_defconfig
make
