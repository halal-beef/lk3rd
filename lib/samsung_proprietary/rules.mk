LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += \
	$(LOCAL_DIR)/tima_smc_handler.S \
	$(LOCAL_DIR)/spi_handler.S

include make/module.mk
