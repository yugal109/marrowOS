#!/bin/bash
# Builds Rain with the MarrowOS platform layer for the host, and runs it on a script
set -e
cd "$(dirname "$0")/.."
OUT=host_test/build
mkdir -p $OUT
SRCS=$(ls src/*.c src/compiler/*.c src/vm/*.c src/constants/*.c | grep -v "src/debug.c\|src/main.c")
INC="-Isrc -Isrc/compiler -Isrc/vm -Isrc/constants -Isrc/platform"
FLAGS="-g -O0 -fno-builtin -fsanitize=address,undefined -Wall -std=gnu99"

# Rain's own files see the shadow headers, the glue sees MarrowOS's real ones
for f in $SRCS src/platform/rain_embed.c; do
    gcc $FLAGS -Isrc/libc $INC -include src/platform/rain_platform.h -c $f -o $OUT/$(echo $f | tr / _).o
done
gcc $FLAGS $INC -Isrc/platform -I../stdlib/src -include src/platform/rain_platform.h -c src/platform/rain_platform.c -o $OUT/platform.o
gcc $FLAGS -c host_test/host_stubs.c -o $OUT/stubs.o
gcc $FLAGS -Isrc/platform -c host_test/host_main.c -o $OUT/main.o
gcc -fsanitize=address,undefined $OUT/*.o -o $OUT/host_rain
