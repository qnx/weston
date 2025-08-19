TEST_NAME = single-pixel-buffer
SRCS += single-pixel-buffer-test.c single-pixel-buffer-v1-protocol.c


define PINFO
PINFO DESCRIPTION = tests/single-pixel-buffer-test
endef

include ../../../../dep_test_client.mk
