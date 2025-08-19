TEST_NAME = keyboard
SRCS += keyboard-test.c input-timestamps-helper.c input-timestamps-unstable-v1-protocol.c


define PINFO
PINFO DESCRIPTION = tests/keyboard-test
endef

include ../../../../dep_test_client.mk
