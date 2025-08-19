TEST_NAME = output-transforms
SRCS += output-transforms-test.c


define PINFO
PINFO DESCRIPTION = tests/output-transforms-test
endef

include ../../../../dep_test_client.mk
