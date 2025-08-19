TEST_NAME = pointer
SRCS += pointer-test.c input-timestamps-helper.c input-timestamps-unstable-v1-protocol.c


define PINFO
PINFO DESCRIPTION = tests/pointer-test
endef

include ../../../../dep_test_client.mk
