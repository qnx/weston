TEST_NAME = custom-env
SRCS += config-parser-test.c
LIBS += socket wayland-server epoll

define PINFO
PINFO DESCRIPTION = tests/config-parser-test
endef

include ../../../../dep_zucmain.mk
