#!/bin/bash

export BUILD_TARGET=AOSP
. sc02e.config

time ./_build-bootimg.sh $1
