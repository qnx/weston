TEST_NAME = presentation
SRCS += presentation-test.c presentation-time-protocol.c


define PINFO
PINFO DESCRIPTION = tests/presentation-test
endef

include ../../../../dep_test_client.mk
