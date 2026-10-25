#!/bin/bash
# Builds the shim (x86_64, for the Intel-only official tool) and the version
# reader (native arch).
set -e
cd "$(dirname "$0")"

clang -arch x86_64 -dynamiclib -o shim/shim.dylib shim/shim.c \
    -framework IOKit -framework CoreFoundation
echo "built shim/shim.dylib"

clang -o tools/read-version tools/read-version.c \
    -framework IOKit -framework CoreFoundation
echo "built tools/read-version"
