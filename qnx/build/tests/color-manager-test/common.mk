TEST_NAME = color-manager
SRCS += color-manager-test.c


define PINFO
PINFO DESCRIPTION = tests/color-manager-test
endef

include ../../../../dep_test_client.mk
