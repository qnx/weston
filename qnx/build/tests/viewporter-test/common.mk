TEST_NAME = viewporter
SRCS += viewporter-test.c


define PINFO
PINFO DESCRIPTION = tests/viewporter-test
endef

include ../../../../dep_test_client.mk
