BUILD_DIR=./build
SRC_DIR=./src
# Rain's core, minus the debug disassembler. platform/ is the MarrowOS glue
SOURCES=$(filter-out $(SRC_DIR)/debug.c $(SRC_DIR)/main.c, $(wildcard $(SRC_DIR)/*.c)) \
	$(wildcard $(SRC_DIR)/compiler/*.c) $(wildcard $(SRC_DIR)/vm/*.c) \
	$(wildcard $(SRC_DIR)/constants/*.c) $(wildcard $(SRC_DIR)/platform/*.c)
# Any header change rebuilds everything: struct layouts are shared, and a stale object would use the old one
HEADERS=$(wildcard $(SRC_DIR)/*.h $(SRC_DIR)/*/*.h)
OBJECTS=$(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SOURCES))
INCLUDES=-I$(SRC_DIR) -I$(SRC_DIR)/compiler -I$(SRC_DIR)/vm -I$(SRC_DIR)/constants -I$(SRC_DIR)/platform -I../stdlib/src
CORE_INCLUDES=-I$(SRC_DIR)/libc $(INCLUDES)
FLAGS= -g -ffreestanding -falign-jumps -falign-functions -falign-labels -falign-loops -fstrength-reduce -fomit-frame-pointer -finline-functions -Wno-unused-function -fno-builtin -Werror -Wno-unused-label -Wno-cpp -Wno-unused-parameter -nostdlib -nostartfiles -nodefaultlibs -Wall -O0 -Iinc
.PHONY: all clean

all: rain.elf

rain.elf: $(OBJECTS)
	x86_64-elf-ld -m elf_x86_64 -relocatable $(OBJECTS) -o ./rain.elf

# rain_platform.h is force-included so Rain's own source needs no changes for the C library calls
$(BUILD_DIR)/platform/rain_platform.o: $(SRC_DIR)/platform/rain_platform.c $(HEADERS)
	@mkdir -p $(dir $@)
	x86_64-elf-gcc $(INCLUDES) -include $(SRC_DIR)/platform/rain_platform.h $(FLAGS) -std=gnu99 -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS)
	@mkdir -p $(dir $@)
	x86_64-elf-gcc $(CORE_INCLUDES) -include $(SRC_DIR)/platform/rain_platform.h $(FLAGS) -std=gnu99 -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)
	rm -f ./rain.elf
