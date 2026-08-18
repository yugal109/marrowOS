FILES=./build/start.o ./build/marrowos.o
INCLUDES=

all: ${FILES}
	i686-elf-ld -m elf_i386  -relocatable ${FILES} -o ./stdlib.elf

./build/start.o: ./src/start.asm
	nasm -f elf ./src/start.asm -o ./build/start.o

./build/marrowos.o: ./src/marrowos.asm
	nasm -f elf ./src/marrowos.asm -o ./build/marrowos.o

clean:
	rm -rf ${FILES}
	rm -f ./stdlib.elf
