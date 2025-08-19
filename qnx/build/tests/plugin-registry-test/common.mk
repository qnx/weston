TEST_NAME = plugin-registry
SRCS += plugin-registry-test.c


define PINFO
PINFO DESCRIPTION = tests/plugin-registry-test
endef

include ../../../../dep_test_client.mk
