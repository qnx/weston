TEST_NAME = subsurface
SRCS += subsurface-test.c


define PINFO
PINFO DESCRIPTION = tests/subsurface-test
endef

include ../../../../dep_test_client.mk
