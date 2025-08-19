TEST_NAME = buffer-transforms
SRCS += buffer-transforms-test.c


define PINFO
PINFO DESCRIPTION = tests/buffer-transforms-test
endef

include ../../../../dep_test_client.mk
