TEST_NAME = surface-global
SRCS += surface-global-test.c


define PINFO
PINFO DESCRIPTION = tests/surface-global-test
endef

include ../../../../dep_test_client.mk
