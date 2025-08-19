TEST_NAME = viewporter-shot
SRCS += viewporter-shot-test.c


define PINFO
PINFO DESCRIPTION = tests/viewporter-shot-test
endef

include ../../../../dep_test_client.mk
