TEST_NAME = custom-env
SRCS += custom-env-test.c


define PINFO
PINFO DESCRIPTION = tests/custom-env-test
endef

include ../../../../dep_test_client.mk
