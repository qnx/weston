EXTRA_INCVPATH += $(DIST_ROOT)/frontend

LIBS += memstream

TEST_NAME = color-metadata-errors
SRCS += color-metadata-errors-test.c


define PINFO
PINFO DESCRIPTION = tests/color-metadata-errors-test
endef

include ../../../../dep_test_client.mk
