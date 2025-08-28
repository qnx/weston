ifndef QCONFIG
QCONFIG=qconfig.mk
endif
include $(QCONFIG)

DIST_ROOT = $(PROJECT_ROOT)/../../../..
INSTALLDIR = usr/lib

EXTRA_INCVPATH += $(DIST_ROOT)/clients
EXTRA_INCVPATH += $(DIST_ROOT)
EXTRA_INCVPATH += $(DIST_ROOT)/include
EXTRA_INCVPATH += $(PROJECT_ROOT)/../../$(OS)
EXTRA_INCVPATH += $(PROJECT_ROOT)/../../$(OS)/protocol
EXTRA_INCVPATH += $(addsuffix /cairo,$(USE_ROOT_INCLUDE))
EXTRA_INCVPATH += $(addsuffix /pixman-1,$(USE_ROOT_INCLUDE))

EXTRA_SRCVPATH += $(DIST_ROOT)/clients
EXTRA_SRCVPATH += $(DIST_ROOT)/shared
EXTRA_SRCVPATH += $(PROJECT_ROOT)/../../$(OS)/protocol

# Stops build system from setting INCVPATH=$(EXTRA_SRCVPATH)
# Will still append EXTRA_INCVPATH as expected. There is a
# signal.h file in weston source conflicting with OS signal.h
INCVPATH = $(empty)

SRCS +=	\
	window.c \
	xdg-shell-unstable-v6-protocol.c \
	xdg-shell-protocol.c \
	color-management-v1-protocol.c \
	text-cursor-position-protocol.c \
	pointer-constraints-unstable-v1-protocol.c \
	relative-pointer-unstable-v1-protocol.c \
	ivi-application-protocol.c \
	viewporter-protocol.c \
	matrix.c \
	tablet-unstable-v2-protocol.c \
	fractional-scale-v1-protocol.c \
	tearing-control-v1-protocol.c \
	ivi-application-protocol.c \


define PINFO
PINFO DESCRIPTION = libs/toytoolkit
endef

include $(MKFILES_ROOT)/qtargets.mk
