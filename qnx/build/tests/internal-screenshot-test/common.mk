TEST_NAME = internal-screenshot
SRCS += internal-screenshot-test.c


define PINFO
PINFO DESCRIPTION = tests/internal-screenshot-test
endef

include ../../../../dep_test_client.mk
