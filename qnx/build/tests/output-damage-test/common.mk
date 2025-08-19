TEST_NAME = output-damage
SRCS += output-damage-test.c


define PINFO
PINFO DESCRIPTION = tests/output-damage-test
endef

include ../../../../dep_test_client.mk
