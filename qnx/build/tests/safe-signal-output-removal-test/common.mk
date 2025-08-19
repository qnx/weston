TEST_NAME = safe-signal-output-removal
EXTRA_SRCVPATH += $(DIST_ROOT)/libweston/shell-utils

SRCS += safe-signal-output-removal-test.c shell-utils.c


define PINFO
PINFO DESCRIPTION = tests/safe-signal-output-removal-test
endef

include ../../../../dep_test_client.mk
