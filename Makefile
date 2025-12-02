# This makefile should allow for web compilation, should I choose to do so. I would need to install emsdk

.PHONY: all clean

PLATFORM ?= PLATFORM_DESKTOP
BUILD_MODE ?= RELEASE
RAYLIB_PATH ?= ..
RAYLIB_SRC_PATH ?= $(RAYLIB_PATH)/src
RAYLIB_LIBTYPE ?= STATIC

# Compiler

CC = gcc
ifeq ($(PLATFORM),$(filter $(PLATFORM),PLATFORM_WEB))
	CC = emcc
endif

# Compiler flags

CFLAGS = -Wall -std=c99 -D_DEFAULT_SOURCE -Wno-missing-braces -Wno-unused-result
ifeq ($(BUILD_MODE),DEBUG)
	CFLAGS += -g -D_DEBUG
else
ifeq ($(PLATFORM),$(filter $(PLATFORM),PLATFORM_WEB))
	CFLAGS += -O3
else
	CFLAGS += -O2
endif
endif

# Include and library paths

INCLUDE_PATHS = -I. -I$(RAYLIB_SRC_PATH) -I$(RAYLIB_SRC_PATH)/external
LDFLAGS = -L$(RAYLIB_SRC_PATH)
ifeq ($(PLATFORM),PLATFORM_WEB)
	LDFLAGS += -sTOTAL_MEMORY=134217728 -sFORCE_FILESYSTEM=1 -sEXPORTED_RUNTIME_METHODS=ccall -sMINIFY_HTML=0
	EXT = .html
endif

# Libraries

ifeq ($(PLATFORM),PLATFORM_DESKTOP)
	LDLIBS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11 -latomic
endif
ifeq ($(PLATFORM),PLATFORM_WEB)
	LDLIBS = $(RAYLIB_SRC_PATH)/libraylib.web.a
endif

# Default target

SRC_CODE = game
all: $(SRC_CODE)

# Generic build pattern

%: %.c
	$(CC) -o $@$(EXT) $< $(CFLAGS) $(INCLUDE_PATHS) $(LDFLAGS) $(LDLIBS) -D$(PLATFORM)

# Clean

clean:
	find . -type f -executable -delete
	rm -fv *.o
ifeq ($(PLATFORM),PLATFORM_WEB)
	rm -f */*.wasm */*.html */*.js */*.data
endif
	@echo Cleaning done

