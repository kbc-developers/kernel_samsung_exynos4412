#!/bin/bash

# get random file script
#
# example) 
#  SPLASH_IMAGE=`./get_boot_splash.sh`
#
# warning)
#
#  this does not check filetype
#   if you use this for boot-splash imege,
#    please select the directory that is in BMP only.
#
#for test
#BOOT_SPLASH_DIR='/home/ma34s/work/SC02C_workSpace/sc02c_kernel_ics/boot-splash' 


if [ -n "$1" ]; then
   BOOT_SPLASH_DIR=$1
fi

if [ -n "$BOOT_SPLASH_DIR" ]; then

  num=`ls $BOOT_SPLASH_DIR | wc -l`
  rnum=`expr $RANDOM % $num`

  echo $rnum 
  array=(`ls ${BOOT_SPLASH_DIR}`)
  #echo "${BOOT_SPLASH_DIR}/${array[$rnum]}"
  
  #for test, this out filename only
  #echo ${array[$rnum]}
  echo get boot-splash: ${array[$rnum]}
  ./generate-boot-splash.sh $BOOT_SPLASH_DIR/${array[$rnum]}
fi

