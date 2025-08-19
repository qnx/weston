TEST_NAME = matrix
SRCS += matrix-test.c


define PINFO
PINFO DESCRIPTION = tests/matrix-test
endef

include ../../../../dep_test_client.mk

LIBS += m
