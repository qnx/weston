TEST_NAME = devices
SRCS += devices-test.c


define PINFO
PINFO DESCRIPTION = tests/devices-test
endef

include ../../../../dep_test_client.mk
