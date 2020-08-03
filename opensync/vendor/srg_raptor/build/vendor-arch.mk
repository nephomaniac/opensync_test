OS_TARGETS += SRG_RAPTOR

ifeq ($(TARGET),SRG_RAPTOR)
PLATFORM=openwrt
VENDOR=srg_raptor
PLATFORM_DIR := platform/$(PLATFORM)
KCONFIG_TARGET ?= $(PLATFORM_DIR)/kconfig/openwrt_generic
ARCH_MK := $(PLATFORM_DIR)/build/$(PLATFORM).mk
endif
