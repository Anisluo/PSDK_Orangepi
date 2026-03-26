# =============================================================================
# PSDK Bridge Daemon — Makefile
#
# Targets:
#   make                  stub build (no PSDK hardware required)
#   make PSDK_REAL=1      real PSDK build (DJI SDK + libusb + openssl)
#   make clean            remove build artefacts
#   make run              stub build + run locally
#
# Platform presets (sets CC, PSDK_LIB_ARCH, UART/NET device):
#   make PSDK_REAL=1 PLATFORM=x86_dev     开发机 x86_64
#   make PSDK_REAL=1 PLATFORM=orangepi    OrangePi Zero3 aarch64
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
CFLAGS  ?= -std=c11 -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
LDFLAGS ?=

BUILD_DIR := build
BIN_DIR   := $(BUILD_DIR)/bin
OBJ_DIR   := $(BUILD_DIR)/obj

# ── Platform config (optional): make PLATFORM=x86_dev or PLATFORM=orangepi ───
ifdef PLATFORM
  include platform/$(PLATFORM)/platform.mk
endif

# ── Drone model selection ─────────────────────────────────────────────────────
# M3E / M3T  : UART + RNDIS network  (DJI_USE_UART_AND_NETWORK_DEVICE)
# M3TD       : UART + USB Bulk gadget (DJI_USE_UART_AND_USB_BULK_DEVICE) [默认]
DRONE_MODEL ?= M3TD

# ── DJI PSDK SDK paths ───────────────────────────────────────────────────────
# 默认按机型选 SDK:
#   M3E / M3T -> 3.9.2 优先
#   M3TD      -> 3.12.0 优先
ifeq ($(DRONE_MODEL),M3TD)
  PSDK_SDK_DIR ?= $(firstword \
	$(wildcard $(HOME)/Desktop/Payload-SDK-3.12.0) \
	$(wildcard $(HOME)/Desktop/Payload-SDK-3.9.2))
  PSDK_PLATFORM_VARIANT ?= manifold3
else
  PSDK_SDK_DIR ?= $(firstword \
	$(wildcard $(HOME)/Desktop/Payload-SDK-3.9.2) \
	$(wildcard $(HOME)/Desktop/Payload-SDK-3.12.0))
  PSDK_PLATFORM_VARIANT ?= manifold2
endif

PSDK_LIB_ARCH ?= x86_64-linux-gnu-gcc
PSDK_LIB_DIR  := $(PSDK_SDK_DIR)/psdk_lib/lib/$(PSDK_LIB_ARCH)
PSDK_INC_DIR  := $(PSDK_SDK_DIR)/psdk_lib/include
M3TD_CONN_MODE ?= dual
PSDK_PLATFORM := $(PSDK_SDK_DIR)/samples/sample_c/platform/linux/$(PSDK_PLATFORM_VARIANT)
PSDK_COMMON   := $(PSDK_SDK_DIR)/samples/sample_c/platform/linux/common
PSDK_UART_HAL := $(PSDK_PLATFORM)/hal

# ── E-Port USB RNDIS network adapter device name ──────────────────────────────
EPORT_NETDEV ?= usb0

ifeq ($(DRONE_MODEL),M3E)
  PSDK_ENABLE_NETWORK  := 1
  PSDK_ENABLE_USB_BULK := 0
  DRONE_CONN_FLAG      := -DCONFIG_HARDWARE_CONNECTION=DJI_USE_UART_AND_NETWORK_DEVICE
  DRONE_UART_DEV       ?= /dev/ttyUSB0
else ifeq ($(DRONE_MODEL),M3T)
  PSDK_ENABLE_NETWORK  := 1
  PSDK_ENABLE_USB_BULK := 0
  DRONE_CONN_FLAG      := -DCONFIG_HARDWARE_CONNECTION=DJI_USE_UART_AND_NETWORK_DEVICE
  DRONE_UART_DEV       ?= /dev/ttyUSB0
else
  # M3TD and other Matrice 3D series: UART + USB Bulk gadget
  PSDK_ENABLE_NETWORK  := 0
  PSDK_ENABLE_USB_BULK := 1
  ifeq ($(PSDK_PLATFORM_VARIANT),manifold3)
    PSDK_UART_HAL      := $(PSDK_SDK_DIR)/samples/sample_c/platform/linux/manifold2/hal
    ifeq ($(M3TD_CONN_MODE),bulk_only)
      DRONE_CONN_FLAG  := -DCONFIG_HARDWARE_CONNECTION=DJI_USE_ONLY_USB_BULK_DEVICE
    else
      DRONE_CONN_FLAG  := -DCONFIG_HARDWARE_CONNECTION=DJI_USE_UART_AND_USB_BULK_DEVICE
    endif
  else
    DRONE_CONN_FLAG    := -DCONFIG_HARDWARE_CONNECTION=DJI_USE_UART_AND_USB_BULK_DEVICE
  endif
  DRONE_UART_DEV       ?= /dev/ttyUSB0
endif

PSDK_ENABLE_USB_BULK ?= 1
PSDK_ENABLE_NETWORK  ?= 0

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
      $(DRONE_CONN_FLAG) \
      -DLINUX_UART_DEV1=\"$(DRONE_UART_DEV)\" \
      -DLINUX_NETWORK_DEV=\"$(EPORT_NETDEV)\" \
      -I$(PSDK_INC_DIR) \
      -I$(PSDK_PLATFORM)/hal \
      -I$(PSDK_UART_HAL) \
      -I$(PSDK_COMMON)/osal

  # SDK sample HAL/OSAL sources compiled into our binary
  PSDK_SRCS := \
      $(PSDK_COMMON)/osal/osal.c \
      $(PSDK_COMMON)/osal/osal_fs.c \
      $(PSDK_COMMON)/osal/osal_socket.c

  ifneq ($(DRONE_CONN_FLAG),-DCONFIG_HARDWARE_CONNECTION=DJI_USE_ONLY_USB_BULK_DEVICE)
    PSDK_SRCS += $(PSDK_UART_HAL)/hal_uart.c
  endif

  ifeq ($(PSDK_ENABLE_NETWORK),1)
    PSDK_SRCS += $(PSDK_PLATFORM)/hal/hal_network.c
  endif
  ifeq ($(PSDK_ENABLE_USB_BULK),1)
    PSDK_SRCS += $(PSDK_PLATFORM)/hal/hal_usb_bulk.c
  endif

  LDFLAGS += \
      -L$(PSDK_LIB_DIR) \
      -lpayloadsdk \
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
