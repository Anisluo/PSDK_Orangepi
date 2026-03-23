# =============================================================================
# PSDK Bridge Daemon — Makefile
#
# Targets:
#   make                  stub build (no PSDK hardware required)
#   make PSDK_REAL=1      real PSDK build (DJI SDK + libusb + openssl)
#   make clean            remove build artefacts
#   make run              stub build + run locally
#
# Cross-compile for OrangePi Zero3 (aarch64):
#   make CC=aarch64-linux-gnu-gcc [PSDK_REAL=1]
#
# Override E-Port USB network interface (default: usb0):
#   make PSDK_REAL=1 EPORT_NETDEV=usb1
#
# Stub dependencies:
#   sudo apt install libjson-c-dev
#
# PSDK_REAL=1 additional dependencies:
#   sudo apt install libusb-1.0-0-dev libssl-dev libjson-c-dev
# =============================================================================

CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L
LDFLAGS ?=

BUILD_DIR := build
BIN_DIR   := $(BUILD_DIR)/bin
OBJ_DIR   := $(BUILD_DIR)/obj

# ── DJI PSDK 3.9.2 SDK paths ─────────────────────────────────────────────────
PSDK_SDK_DIR  ?= /home/aniston/Desktop/Payload-SDK-3.9.2
PSDK_LIB_DIR  := $(PSDK_SDK_DIR)/psdk_lib/lib/aarch64-linux-gnu-gcc
PSDK_INC_DIR  := $(PSDK_SDK_DIR)/psdk_lib/include
PSDK_PLATFORM := $(PSDK_SDK_DIR)/samples/sample_c/platform/linux/manifold2
PSDK_COMMON   := $(PSDK_SDK_DIR)/samples/sample_c/platform/linux/common

# ── E-Port USB RNDIS network adapter device name (OrangePi Zero3 default) ────
EPORT_NETDEV ?= usb0

# ── Include paths (always) ────────────────────────────────────────────────────
INCLUDES := -Iinclude -Ibsp -Icore
CFLAGS   += $(INCLUDES)
LDFLAGS  += -ljson-c -lm -lpthread

# ── Application sources (always) ─────────────────────────────────────────────
APP_SRCS := \
	app/main.c \
	core/log/log.c \
	core/server/udp_server.c \
	core/proto/rpc.c \
	core/handler/handler.c \
	bsp/psdk_hal.c \
	bsp/psdk_init.c \
	bsp/drone_ctrl.c

# ══════════════════════════════════════════════════════════════════════════════
# PSDK_REAL=1 — link real DJI PSDK; compile SDK sample HAL/OSAL
# ══════════════════════════════════════════════════════════════════════════════
ifdef PSDK_REAL
  CFLAGS += \
      -DPSDK_REAL \
      -DLINUX_NETWORK_DEV=\"$(EPORT_NETDEV)\" \
      -I$(PSDK_INC_DIR) \
      -I$(PSDK_PLATFORM)/hal \
      -I$(PSDK_COMMON)/osal

  # SDK sample HAL/OSAL sources compiled into our binary
  PSDK_SRCS := \
      $(PSDK_PLATFORM)/hal/hal_uart.c \
      $(PSDK_PLATFORM)/hal/hal_network.c \
      $(PSDK_COMMON)/osal/osal.c \
      $(PSDK_COMMON)/osal/osal_fs.c \
      $(PSDK_COMMON)/osal/osal_socket.c

  LDFLAGS += \
      -L$(PSDK_LIB_DIR) \
      -lpayloadsdk \
      -lusb-1.0 \
      -lssl -lcrypto \
      -lrt

  ALL_SRCS := $(APP_SRCS) $(PSDK_SRCS)
else
  ALL_SRCS := $(APP_SRCS)
endif

# ── Object file mapping ───────────────────────────────────────────────────────
# Project files:  src/foo.c  → build/obj/src/foo.o
# SDK files:      /path/to/sdk/foo.c → build/obj/sdk/foo.o  (strip SDK prefix)
define src_to_obj
$(if $(filter $(PSDK_SDK_DIR)/%,$(1)),\
    $(OBJ_DIR)/sdk/$(patsubst $(PSDK_SDK_DIR)/%,%,$(1:.c=.o)),\
    $(OBJ_DIR)/$(1:.c=.o))
endef

OBJS := $(foreach s,$(ALL_SRCS),$(call src_to_obj,$(s)))
BIN  := $(BIN_DIR)/psdkd

# ── Rules ─────────────────────────────────────────────────────────────────────
.PHONY: all clean run dirs

all: dirs $(BIN)

dirs:
	@mkdir -p \
	    $(BIN_DIR) \
	    $(OBJ_DIR)/app \
	    $(OBJ_DIR)/core/log \
	    $(OBJ_DIR)/core/server \
	    $(OBJ_DIR)/core/proto \
	    $(OBJ_DIR)/core/handler \
	    $(OBJ_DIR)/bsp \
	    $(OBJ_DIR)/sdk/samples/sample_c/platform/linux/manifold2/hal \
	    $(OBJ_DIR)/sdk/samples/sample_c/platform/linux/common/osal

$(BIN): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@echo "Built: $@"

# Project sources (relative paths)
$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# SDK sample sources (absolute paths → OBJ_DIR/sdk/...)
$(OBJ_DIR)/sdk/%.o: $(PSDK_SDK_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

run: all
	$(BIN) --debug

clean:
	rm -rf $(BUILD_DIR)
