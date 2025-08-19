TEST_NAME = string
SRCS += string-test.c


define PINFO
PINFO DESCRIPTION = tests/string-test
endef

include ../../../../dep_test_client.mk
