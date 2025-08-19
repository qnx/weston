TEST_NAME = safe-signal
SRCS += safe-signal-test.c


define PINFO
PINFO DESCRIPTION = tests/safe-signal-test
endef

include ../../../../dep_test_client.mk
