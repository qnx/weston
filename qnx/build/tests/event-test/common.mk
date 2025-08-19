TEST_NAME = event
SRCS += event-test.c


define PINFO
PINFO DESCRIPTION = tests/event-test
endef

include ../../../../dep_test_client.mk
