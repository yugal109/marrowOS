BUILD_DIR=./build
FILES=$(BUILD_DIR)/settings.o
INCLUDES=-I../stdlib/src -I../guilib/src -I../containerlib/src
FLAGS= -g -ffreestanding -falign-jumps -falign-functions -falign-labels -falign-loops -fstrength-reduce -fomit-frame-pointer -finline-functions -Wno-unused-function -fno-builtin -Werror -Wno-unused-label -Wno-cpp -Wno-unused-parameter -nostdlib -nostartfiles -nodefaultlibs -Wall -O0 -Iinc
.PHONY: all clean

all: settings.elf

settings.elf: ${FILES}
	x86_64-elf-gcc -g -T ./linker.ld -o ./settings.elf -ffreestanding -O0 -nostdlib -fpic -g ${FILES} ../containerlib/containerlib.elf ../stdlib/stdlib.elf ../guilib/guilib.elf

$(BUILD_DIR)/settings.o: ./settings.c
	mkdir -p $(BUILD_DIR)
	x86_64-elf-gcc ${INCLUDES} -I./ $(FLAGS) -std=gnu99 -c ./settings.c -o $(BUILD_DIR)/settings.o

clean:
	rm -rf ${FILES}
	rm -f ./settings.elf
