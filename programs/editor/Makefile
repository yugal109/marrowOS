BUILD_DIR=./build
FILES=$(BUILD_DIR)/editor.o
INCLUDES=-I../stdlib/src -I../guilib/src -I../containerlib/src -I../rain/src/platform
FLAGS= -g -ffreestanding -falign-jumps -falign-functions -falign-labels -falign-loops -fstrength-reduce -fomit-frame-pointer -finline-functions -Wno-unused-function -fno-builtin -Werror -Wno-unused-label -Wno-cpp -Wno-unused-parameter -nostdlib -nostartfiles -nodefaultlibs -Wall -O0 -Iinc
.PHONY: all clean

all: editor.elf

editor.elf: ${FILES} ../rain/rain.elf
	x86_64-elf-gcc -g -T ./linker.ld -o ./editor.elf -ffreestanding -O0 -nostdlib -fpic -g ${FILES} ../containerlib/containerlib.elf ../stdlib/stdlib.elf ../guilib/guilib.elf ../rain/rain.elf

$(BUILD_DIR)/editor.o: ./editor.c ../rain/src/platform/rain.h
	mkdir -p $(BUILD_DIR)
	x86_64-elf-gcc ${INCLUDES} -I./ $(FLAGS) -std=gnu99 -c ./editor.c -o $(BUILD_DIR)/editor.o

clean:
	rm -rf ${FILES}
	rm -f ./editor.elf
