#!/bin/bash

KERNEL_DIR=$PWD
export BUILD_DEVICE=$1

if [ "$BUILD_DEVICE" = "SC03ESAM" -o "$BUILD_DEVICE" = "SC03EAOSP" ];then
	echo "=====> Build Device SC-03E"
	INITRAMFS_SRC_DIR=../sc03e_ramdisk
else
	echo "=====> Build Device SC-02E"
	INITRAMFS_SRC_DIR=../sc02e_ramdisk
fi

if [ -z "$INITRAMFS_TMP_DIR" ]; then
if [ "$BUILD_DEVICE" = "SC03ESAM" -o "$BUILD_DEVICE" = "SC03EAOSP" ];then
	INITRAMFS_TMP_DIR=/tmp/sc03e_initramfs
else
	INITRAMFS_TMP_DIR=/tmp/sc02e_initramfs
fi
fi

cpoy_initramfs()
{
  echo copy to $INITRAMFS_TMP_DIR ... $(dirname $INITRAMFS_TMP_DIR)
  
  if [ ! -d $(dirname $INITRAMFS_TMP_DIR) ]; then
    mkdir -p $(dirname $INITRAMFS_TMP_DIR)
  fi

  if [ -d $INITRAMFS_TMP_DIR ]; then
    rm -rf $INITRAMFS_TMP_DIR  
  fi
  cp -a $INITRAMFS_SRC_DIR $INITRAMFS_TMP_DIR
  rm -rf $INITRAMFS_TMP_DIR/.git
  find $INITRAMFS_TMP_DIR -name .gitignore | xargs rm


#  if [ "$BUILD_DEVICE" = "MULTI" ]; then
#    BOOT_MODE=1
#  else
#    BOOT_MODE=0
#  fi
#
#  if [ -z `grep IS_MODE= $INITRAMFS_TMP_DIR/init` ]; then
#    sed -i -e s/BOOT_MODE=./BOOT_MODE=$BOOT_MODE/g $INITRAMFS_TMP_DIR/init    
#  else
#    sed -i -e s/IS_MODE=./IS_MODE=$BOOT_MODE/g $INITRAMFS_TMP_DIR/init
#  fi
}

# check target
# (note) MULTI and COM use same defconfig
BUILD_TARGET=$1
case "$BUILD_TARGET" in
  "SC02EAOSP" ) BUILD_DEFCONFIG=sc02e_aosp_defconfig ;;
  "SC02ESAM" ) BUILD_DEFCONFIG=sc02e_samsung_defconfig ;;
  "SC03EAOSP" ) BUILD_DEFCONFIG=sc03e_aosp_defconfig ;;
  "SC03ESAM" ) BUILD_DEFCONFIG=sc03e_samsung_defconfig ;;
  * ) echo "error: not found BUILD_TARGET" && exit -1 ;;
esac

BIN_DIR=out/$BUILD_TARGET/bin
OBJ_DIR=out/$BUILD_TARGET/obj
mkdir -p $BIN_DIR
mkdir -p $OBJ_DIR

# boot splash header
#if [ -f ./drivers/video/samsung/logo_rgb24_user.h ]; then
#  export USER_BOOT_SPLASH=y
#fi

# generate LOCALVERSION
if [ "$BUILD_DEVICE" = "SC03ESAM" -o "$BUILD_DEVICE" = "SC03EAOSP" ];then
. mod_version_sc03e
else
. mod_version_sc02e
fi

if [ -n "$BUILD_NUMBER" ]; then
export KBUILD_BUILD_VERSION="$BUILD_NUMBER"
fi
# check and get compiler
. cross_compile

# set build env
export ARCH=arm
export CROSS_COMPILE=$BUILD_CROSS_COMPILE
export USE_SEC_FIPS_MODE=true
export LOCALVERSION="-$BUILD_LOCALVERSION"

echo "=====> BUILD START $BUILD_KERNELVERSION-$BUILD_LOCALVERSION"

if [ ! -n "$2" ]; then
  echo ""
  read -p "select build? [(a)ll/(u)pdate/(z)Image default:update] " BUILD_SELECT
else
  BUILD_SELECT=$2
fi

# copy initramfs
echo ""
echo "=====> copy initramfs"
cpoy_initramfs


# make start
if [ "$BUILD_SELECT" = 'all' -o "$BUILD_SELECT" = 'a' ]; then
  echo ""
  echo "=====> cleaning"
  rm -rf out
  mkdir -p $BIN_DIR
  mkdir -p $OBJ_DIR
  cp -f ./arch/arm/configs/$BUILD_DEFCONFIG $OBJ_DIR/.config
  make -C $PWD O=$OBJ_DIR oldconfig || exit -1
fi

if [ "$BUILD_SELECT" != 'zImage' -a "$BUILD_SELECT" != 'z' ]; then
  echo ""
  echo "=====> build start"
  if [ -e make.log ]; then
    mv make.log make_old.log
  fi
  nice -n 10 make O=$OBJ_DIR -j12 2>&1 | tee make.log
fi

# check compile error
COMPILE_ERROR=`grep 'error:' ./make.log`
if [ "$COMPILE_ERROR" ]; then
  echo ""
  echo "=====> ERROR"
  grep 'error:' ./make.log
  exit -1
fi

# *.ko replace
find -name '*.ko' -exec cp -av {} $INITRAMFS_TMP_DIR/lib/modules/ \;

# build zImage
echo ""
echo "=====> make zImage"
nice -n 10 make O=$OBJ_DIR -j2 zImage CONFIG_INITRAMFS_SOURCE="$INITRAMFS_TMP_DIR" CONFIG_INITRAMFS_ROOT_UID=`id -u` CONFIG_INITRAMFS_ROOT_GID=`id -g` || exit 1

if [ ! -e $OUTPUT_DIR ]; then
  mkdir -p $OUTPUT_DIR
fi

echo ""
echo "=====> CREATE RELEASE IMAGE"
# clean release dir
if [ `find $BIN_DIR -type f | wc -l` -gt 0 ]; then
  rm $BIN_DIR/*
fi

# copy zImage
cp $OBJ_DIR/arch/arm/boot/zImage $BIN_DIR/zImage
cp $OBJ_DIR/arch/arm/boot/zImage ./out/
echo "  $BIN_DIR/zImage"
echo "  out/zImage"

# create odin image
cd $KERNEL_DIR/$BIN_DIR
tar cf $BUILD_LOCALVERSION-odin.tar zImage
md5sum -t $BUILD_LOCALVERSION-odin.tar >> $BUILD_LOCALVERSION-odin.tar
mv $BUILD_LOCALVERSION-odin.tar $BUILD_LOCALVERSION-odin.tar.md5
echo "  $BIN_DIR/$BUILD_LOCALVERSION-odin.tar.md5"

# create cwm image
cd $KERNEL_DIR/$BIN_DIR
if [ -d tmp ]; then
  rm -rf tmp
fi
mkdir -p ./tmp/META-INF/com/google/android
cp zImage ./tmp/
cp $KERNEL_DIR/release-tools/update-binary ./tmp/META-INF/com/google/android/
if [ "$BUILD_DEVICE" = "N7000SAM" -o "$BUILD_DEVICE" = "N7000AOSP" -o "$BUILD_DEVICE" = "N7000JB" ];then
sed -e "s/@VERSION/$BUILD_LOCALVERSION/g" $KERNEL_DIR/release-tools/updater-script-n7000.sed > ./tmp/META-INF/com/google/android/updater-script
else
sed -e "s/@VERSION/$BUILD_LOCALVERSION/g" $KERNEL_DIR/release-tools/updater-script.sed > ./tmp/META-INF/com/google/android/updater-script
fi
cd tmp && zip -rq ../cwm.zip ./* && cd ../
SIGNAPK_DIR=$KERNEL_DIR/release-tools/signapk
java -jar $SIGNAPK_DIR/signapk.jar $SIGNAPK_DIR/testkey.x509.pem $SIGNAPK_DIR/testkey.pk8 cwm.zip $BUILD_LOCALVERSION-signed.zip
rm cwm.zip
rm -rf tmp
echo "  $BIN_DIR/$BUILD_LOCALVERSION-signed.zip"

#cleanup
rm $KERNEL_DIR/$BIN_DIR/zImage

cd $KERNEL_DIR
echo ""
echo "=====> BUILD COMPLETE $BUILD_KERNELVERSION-$BUILD_LOCALVERSION"
exit 0
