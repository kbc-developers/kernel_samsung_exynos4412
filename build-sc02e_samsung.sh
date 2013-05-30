#!/bin/bash

export BUILD_TARGET=SAM
. sc02e.config

time ./_build-bootimg.sh $1
