TEST_NAME = touch
SRCS += touch-test.c input-timestamps-helper.c input-timestamps-unstable-v1-protocol.c


define PINFO
PINFO DESCRIPTION = tests/touch-test
endef

include ../../../../dep_test_client.mk
