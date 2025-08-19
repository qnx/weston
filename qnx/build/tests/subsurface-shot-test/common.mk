TEST_NAME = subsurface-shot
SRCS += subsurface-shot-test.c


define PINFO
PINFO DESCRIPTION = tests/subsurface-shot-test
endef

include ../../../../dep_test_client.mk
