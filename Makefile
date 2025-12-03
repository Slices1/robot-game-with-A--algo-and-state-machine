.PHONY: all clean

# Default settings
PLATFORM ?= PLATFORM_DESKTOP
BUILD_MODE ?= RELEASE

# Compiler
CC = gcc
ifeq ($(PLATFORM),PLATFORM_WEB)
    CC = emcc
endif

# Compiler flags
CFLAGS = -Wall -std=c99 -D_DEFAULT_SOURCE -Wno-missing-braces -Wno-unused-result
ifeq ($(BUILD_MODE),DEBUG)
    CFLAGS += -g -D_DEBUG
else
    ifeq ($(PLATFORM),PLATFORM_WEB)
        CFLAGS += -O3
    else
        CFLAGS += -O2
    endif
endif

# Paths
# GCC automatically checks /usr/local/include and /usr/local/lib
INCLUDE_PATHS = -I.
LDFLAGS = -L.

ifeq ($(PLATFORM),PLATFORM_WEB)
    LDFLAGS += -s USE_GLFW=3 -s ASYNCIFY -s TOTAL_MEMORY=67108864 -s FORCE_FILESYSTEM=1
    EXT = .html
endif

# Libraries
ifeq ($(PLATFORM),PLATFORM_DESKTOP)
    # Standard flags for Raylib on Linux
    LDLIBS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
endif

# Sources
SRC = game.c
TARGET = game

# Build Rules
all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) -o $@$(EXT) $< $(CFLAGS) $(INCLUDE_PATHS) $(LDFLAGS) $(LDLIBS) -D$(PLATFORM)

clean:
	rm -f $(TARGET) $(TARGET).html $(TARGET).js $(TARGET).wasm $(TARGET).data *.o
	@echo Cleaning done