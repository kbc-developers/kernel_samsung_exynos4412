#!/bin/bash

export BUILD_TARGET=AOSP
. sc03e.config

time ./_build-bootimg.sh $1
