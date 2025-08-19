TEST_NAME = surface
SRCS += surface-test.c


define PINFO
PINFO DESCRIPTION = tests/surface-test
endef

include ../../../../dep_test_client.mk
