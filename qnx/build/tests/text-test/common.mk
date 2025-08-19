TEST_NAME = text
SRCS += text-test.c text-input-unstable-v1-protocol.c


define PINFO
PINFO DESCRIPTION = tests/text-test
endef

include ../../../../dep_test_client.mk

LIBS += m
