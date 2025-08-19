EXTRA_INCVPATH += $(addsuffix /libdrm,$(USE_ROOT_INCLUDE))

TEST_NAME = yuv-buffer
SRCS += yuv-buffer-test.c


define PINFO
PINFO DESCRIPTION = tests/yuv-buffer-test
endef

include ../../../../dep_test_client.mk

LIBS += m
