BUILD_DIR=./build
FILES=$(BUILD_DIR)/draw.o $(BUILD_DIR)/print.o
INCLUDES=-I../stdlib/src -I../guilib/src -I../containerlib/src
FLAGS= -g -ffreestanding -falign-jumps -falign-functions -falign-labels -falign-loops -fstrength-reduce -fomit-frame-pointer -finline-functions -Wno-unused-function -fno-builtin -Werror -Wno-unused-label -Wno-cpp -Wno-unused-parameter -nostdlib -nostartfiles -nodefaultlibs -Wall -O0 -Iinc
.PHONY: all clean

all: draw.elf

draw.elf: ${FILES}
	x86_64-elf-gcc -g -T ./linker.ld -o ./draw.elf -ffreestanding -O0 -nostdlib -fpic -g ${FILES} ../containerlib/containerlib.elf ../stdlib/stdlib.elf ../guilib/guilib.elf

$(BUILD_DIR)/print.o: ./print.c ./print.h
	mkdir -p $(BUILD_DIR)
	x86_64-elf-gcc ${INCLUDES} -I./ $(FLAGS) -std=gnu99 -c ./print.c -o $(BUILD_DIR)/print.o

$(BUILD_DIR)/draw.o: ./draw.c ./print.h
	mkdir -p $(BUILD_DIR)
	x86_64-elf-gcc ${INCLUDES} -I./ $(FLAGS) -std=gnu99 -c ./draw.c -o $(BUILD_DIR)/draw.o

clean:
	rm -rf ${FILES}
	rm -f ./draw.elf
