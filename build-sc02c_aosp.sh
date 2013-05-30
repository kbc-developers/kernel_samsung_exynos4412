#!/bin/bash

export BUILD_TARGET=AOSP
. sc02c.config

time ./_build-bootimg.sh $1
