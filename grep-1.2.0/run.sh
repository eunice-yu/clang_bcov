#!/bin/bash
#set -x
#./$1 -extra-arg=-I/usr/local/src/llvm/build/lib/clang/5.0.0/include $2
../bcov/build/bcov -extra-arg=-I/usr/local/src/llvm/build/lib/clang/5.0.0/include grep.c > grep_bcov.c 

