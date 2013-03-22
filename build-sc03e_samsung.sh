#!/bin/bash

export BUILD_TARGET=SAM
. sc03e.config

time ./_build-bootimg.sh $1
