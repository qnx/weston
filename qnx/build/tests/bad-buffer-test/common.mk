TEST_NAME = bad-buffer
SRCS += bad-buffer-test.c


define PINFO
PINFO DESCRIPTION = tests/bad-buffer-test
endef

include ../../../../dep_test_client.mk
