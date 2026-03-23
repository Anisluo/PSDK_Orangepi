# ─────────────────────────────────────────────────────────────────────────────
# PSDK Bridge Daemon — Makefile
#
# Targets:
#   make            build stub (no PSDK hardware required)
#   make PSDK_REAL=1 build with real DJI PSDK (requires SDK in PSDK_SDK_DIR)
#   make clean      remove build artefacts
#   make run        build + run stub locally
#
# Cross-compilation for OrangePi Zero3 (aarch64):
#   make CC=aarch64-linux-gnu-gcc
#
# Dependencies (stub mode):
#   libjson-c-dev    sudo apt install libjson-c-dev
#
# Dependencies (PSDK_REAL=1, add to above):
#   DJI PSDK SDK     set PSDK_SDK_DIR to the extracted SDK root
# ─────────────────────────────────────────────────────────────────────────────

CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -O2 -D_POSIX_C_SOURCE=200809L
LDFLAGS ?=

BUILD_DIR := build
BIN_DIR   := $(BUILD_DIR)/bin
OBJ_DIR   := $(BUILD_DIR)/obj

# ── Include paths ─────────────────────────────────────────────────────────────
INCLUDES := -Iinclude -Icore

# ── json-c ────────────────────────────────────────────────────────────────────
CFLAGS  += $(INCLUDES)
LDFLAGS += -ljson-c -lm

# ── PSDK real build ───────────────────────────────────────────────────────────
ifdef PSDK_REAL
  PSDK_SDK_DIR ?= $(HOME)/psdk
  CFLAGS  += -DPSDK_REAL \
             -I$(PSDK_SDK_DIR)/psdk_lib/include \
             -I$(PSDK_SDK_DIR)/samples/sample_c/platform/linux/common/osal \
             -I$(PSDK_SDK_DIR)/samples/sample_c/platform/linux/common/hal
  LDFLAGS += -L$(PSDK_SDK_DIR)/psdk_lib/lib/aarch64-linux-gnu-gcc \
             -lpayloadsdk -lpthread -lusb-1.0 -lssl -lcrypto
endif

# ── Sources ───────────────────────────────────────────────────────────────────
SRCS := \
	app/main.c \
	core/log/log.c \
	core/server/tcp_server.c \
	core/proto/rpc.c \
	core/handler/handler.c \
	bsp/psdk_hal.c \
	bsp/psdk_init.c \
	bsp/drone_ctrl.c

OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(SRCS))
BIN  := $(BIN_DIR)/psdkd

# ── Rules ─────────────────────────────────────────────────────────────────────
.PHONY: all clean run dirs

all: dirs $(BIN)

dirs:
	@mkdir -p $(BIN_DIR) \
		$(OBJ_DIR)/app \
		$(OBJ_DIR)/core/log \
		$(OBJ_DIR)/core/server \
		$(OBJ_DIR)/core/proto \
		$(OBJ_DIR)/core/handler \
		$(OBJ_DIR)/bsp

$(BIN): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@echo "Built: $@"

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

run: all
	$(BIN) --debug

clean:
	rm -rf $(BUILD_DIR)
