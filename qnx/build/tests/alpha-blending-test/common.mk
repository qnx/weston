TEST_NAME = alpha-blending
SRCS += alpha-blending-test.c


define PINFO
PINFO DESCRIPTION = tests/alpha-blending-test
endef

include ../../../../dep_test_client.mk

LIBS += m

