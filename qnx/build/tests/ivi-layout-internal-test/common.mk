TEST_NAME = ivi-layout-internal
SRCS += ivi-layout-internal-test.c


define PINFO
PINFO DESCRIPTION = tests/ivi-layout-internal-test
endef

include ../../../../dep_test_client.mk
