TEST_NAME = ivi-layout-client
SRCS += ivi-layout-test-client.c ivi-application-protocol.c


define PINFO
PINFO DESCRIPTION = tests/ivi-layout-client-test
endef

include ../../../../dep_test_client.mk
