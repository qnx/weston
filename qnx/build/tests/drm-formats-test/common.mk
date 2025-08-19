EXTRA_INCVPATH += $(addsuffix /libdrm,$(USE_ROOT_INCLUDE))

TEST_NAME = drm-formats
SRCS += drm-formats-test.c


define PINFO
PINFO DESCRIPTION = tests/drm-formats-test
endef

include ../../../../dep_test_client.mk
