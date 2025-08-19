TEST_NAME = roles
SRCS += roles-test.c xdg-shell-protocol.c


define PINFO
PINFO DESCRIPTION = tests/roles-test
endef

include ../../../../dep_test_client.mk
