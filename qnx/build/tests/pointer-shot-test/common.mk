TEST_NAME = pointer-shot
SRCS += pointer-shot-test.c


define PINFO
PINFO DESCRIPTION = tests/pointer-shot-test
endef

include ../../../../dep_test_client.mk
