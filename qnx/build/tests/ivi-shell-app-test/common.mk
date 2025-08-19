TEST_NAME = ivi-shell-app
SRCS += ivi-shell-app-test.c ivi-application-protocol.c


define PINFO
PINFO DESCRIPTION = tests/ivi-shell-app-test
endef

include ../../../../dep_test_client.mk
