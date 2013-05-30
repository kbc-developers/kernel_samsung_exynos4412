#!/bin/bash

KERNEL_DIR=$PWD
IMAGE_NAME=boot

BIN_DIR=out/$TARGET_DEVICE/$BUILD_TARGET/bin
OBJ_DIR=out/$TARGET_DEVICE/$BUILD_TARGET/obj
mkdir -p $BIN_DIR
mkdir -p $OBJ_DIR

. build_func
. mod_version
. cross_compile

# jenkins build number
if [ -n "$BUILD_NUMBER" ]; then
export KBUILD_BUILD_VERSION="$BUILD_NUMBER"
fi

# set build env
export ARCH=arm
export CROSS_COMPILE=$BUILD_CROSS_COMPILE
export LOCALVERSION="-$BUILD_LOCALVERSION"

echo ""
echo "====================================================================="
echo "    BUILD START (KERNEL VERSION $BUILD_KERNELVERSION-$BUILD_LOCALVERSION)"
echo "====================================================================="

if [ ! -n "$1" ]; then
  echo ""
  read -p "select build? [(a)ll/(u)pdate/(i)mage default:update] " BUILD_SELECT
else
  BUILD_SELECT=$1
fi

echo "BOOT_RAMDISK_SRC_DIR=$BOOT_RAMDISK_SRC_DIR"
# copy RAMDISK
echo ""
echo "=====> COPY RAMDISK"
if [ "$USE_INITRAMFS" = 'y' ]; then
  copy_ramdisk $BOOT_RAMDISK_SRC_DIR $BOOT_RAMDISK_TMP_DIR
  copy_ramdisk $RECO_RAMDISK_SRC_DIR $RECO_RAMDISK_TMP_DIR
else
  copy_ramdisk $RAMDISK_SRC_DIR $RAMDISK_TMP_DIR
fi

# make start
if [ "$BUILD_SELECT" = 'all' -o "$BUILD_SELECT" = 'a' ]; then
  echo ""
  echo "=====> CLEANING..."
  make clean
  cp -f ./arch/arm/configs/$KERNEL_DEFCONFIG $OBJ_DIR/.config
  make -C $PWD O=$OBJ_DIR oldconfig || exit -1
fi

if [ "$BUILD_SELECT" != 'image' -a "$BUILD_SELECT" != 'i' ]; then
  if [ -e make.log ]; then
    mv make.log make_old.log
  fi
  if [ "$USE_INITRAMFS" = 'y' ]; then
    echo ""
    echo "=====> MAKE KERNEL MODULE.."
    nice -n 10 make O=$OBJ_DIR -j12 modules 2>&1 | tee make.log
  else
    echo ""
    echo "=====> MAKE KERNEL IMAGE..."
    nice -n 10 make O=$OBJ_DIR -j12 2>&1 | tee make.log
  fi
fi

# check compile error
COMPILE_ERROR=`grep 'error:' ./make.log`
if [ "$COMPILE_ERROR" ]; then
  echo ""
  echo "=====> ERROR"
  grep 'error:' ./make.log
  exit -1
fi

# *.ko install
echo ""
echo "=====> INSTALL KERNEL MODULES"
if [ "$USE_INITRAMFS" = 'y' ]; then
  find -name '*.ko' -exec cp -av {} $BOOT_RAMDISK_TMP_DIR/lib/modules/ \;
  STRIP=strip
  $CROSS_COMPILE$STRIP --strip-unneeded $BOOT_RAMDISK_TMP_DIR/lib/modules/*
else
  find -name '*.ko' -exec cp -av {} $RAMDISK_TMP_DIR/lib/modules/ \;
fi

if [ "$USE_INITRAMFS" = 'y' ]; then
  echo ""
  echo "=====> GENERATE INITRAMFS"
  generate_initramfs
  echo ""
  echo "=====> MAKE KERNEL IMAGE..."
  nice -n 10 make O=$OBJ_DIR -j12 2>&1 | tee make.log
fi

echo ""
echo "=====> CREATE RELEASE IMAGE"
# clean release dir
if [ `find $BIN_DIR -type f | wc -l` -gt 0 ]; then
  rm -rf $BIN_DIR/*
fi
mkdir -p $BIN_DIR

# copy zImage -> kernel
cp $OBJ_DIR/arch/arm/boot/zImage $BIN_DIR/kernel

# create boot image
make_boot_image

# create odin image
cd $BIN_DIR
make_odin3_image

# create cwm image
make_cwm_image

cd $KERNEL_DIR

echo ""
echo "====================================================================="
echo "    BUILD COMPLETED"
echo "====================================================================="
exit 0
