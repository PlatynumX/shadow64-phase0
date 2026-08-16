V=1
SOURCE_DIR=src
BUILD_DIR=build
FILESYSTEM_DIR=filesystem
PROJECT=shadow64_phase0_r11

include $(N64_INST)/include/n64.mk

all: $(PROJECT).z64
.PHONY: all

OBJS = $(BUILD_DIR)/main.o
DFS_FILES = $(wildcard $(FILESYSTEM_DIR)/*)

$(PROJECT).z64: N64_ROM_TITLE="Shadow64 P0 R11"
$(PROJECT).z64: $(BUILD_DIR)/$(PROJECT).dfs

$(BUILD_DIR)/$(PROJECT).dfs: $(DFS_FILES) filesystem/sw_first.map filesystem/sw_r13_tex.bin

$(BUILD_DIR)/$(PROJECT).elf: $(OBJS)

clean:
	rm -rf $(BUILD_DIR) *.z64
.PHONY: clean

-include $(wildcard $(BUILD_DIR)/*.d)

# Shadow64 R13A Performance Gameplay: user-derived map and compact ART texture bank are packed into DragonFS.
