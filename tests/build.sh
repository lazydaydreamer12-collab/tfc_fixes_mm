#!/bin/sh
# Builds the test harness (32-bit). Needs g++-multilib, and the metamod-p SDK
# where the plugin's Makefile.linux finds it.
cd "$(dirname "$0")"
SDK=${SDK:-$(if [ -f ../../metamod/meta_api.h ]; then echo ../..; else echo ../../../sdk/metamod-p; fi)}
g++ -m32 -g -O0 -std=gnu++11 -w -fpermissive \
    -I$SDK/metamod -I$SDK/hlsdk/dlls -I$SDK/hlsdk/engine -I$SDK/hlsdk/common -I$SDK/hlsdk/pm_shared \
    fxsim.cpp t_fixes.cpp -o t_fixes -ldl
