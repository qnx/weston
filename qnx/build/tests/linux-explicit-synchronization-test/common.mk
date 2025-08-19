TEST_NAME = linux-explicit-synchronization
SRCS += linux-explicit-synchronization-test.c linux-explicit-synchronization-unstable-v1-protocol.c


define PINFO
PINFO DESCRIPTION = tests/linux-explicit-synchronization-test
endef

include ../../../../dep_test_client.mk
