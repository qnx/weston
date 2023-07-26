ifndef QCONFIG
QCONFIG=qconfig.mk
endif
include $(QCONFIG)

DIST_ROOT = $(PROJECT_ROOT)/../../../..
INSTALLDIR = usr/lib/weston

EXTRA_INCVPATH += $(DIST_ROOT)/fullscreen-shell
EXTRA_INCVPATH += $(DIST_ROOT)/shell-utils
EXTRA_INCVPATH += $(DIST_ROOT)/include
EXTRA_INCVPATH += $(DIST_ROOT)
EXTRA_INCVPATH += $(PROJECT_ROOT)/../../$(OS)
EXTRA_INCVPATH += $(PROJECT_ROOT)/../../$(OS)/protocol
EXTRA_INCVPATH += $(addsuffix /pixman-1,$(USE_ROOT_INCLUDE))

EXTRA_SRCVPATH += $(DIST_ROOT)/fullscreen-shell
EXTRA_SRCVPATH += $(PROJECT_ROOT)/../../$(OS)/protocol

EXTRA_LIBVPATH += $(PROJECT_ROOT)/../shared/$(OS)/$(CPU)/$(shell echo $(VARIANT1) | sed 's/^dll\(.*\)$$/a.shared\1/')
EXTRA_LIBVPATH += $(PROJECT_ROOT)/../weston/$(OS)/$(CPU)/$(shell echo $(VARIANT1) | sed 's/^dll\(.*\)$$/so\1/')
EXTRA_LIBVPATH += $(PROJECT_ROOT)/../weston-exec/$(OS)/$(CPU)/$(shell echo $(VARIANT1) | sed 's/^dll\(.*\)$$/so\1/')
EXTRA_LIBVPATH += $(PROJECT_ROOT)/../weston-desktop/$(OS)/$(CPU)/$(shell echo $(VARIANT1) | sed 's/^dll\(.*\)$$/so\1/')

# Hide symbols from static libraries.
LDFLAGS += -Wl,--exclude-libs,ALL
# Report unresolved symbols at build time
LDFLAGS += -Wl,--unresolved-symbols=report-all

SRCS += \
	fullscreen-shell.c \
	fullscreen-shell-unstable-v1-protocol.c

LIBS += weston weston-exec sharedS wayland-server pixman-1 m

define PINFO
PINFO DESCRIPTION = Weston Full-screen Shell Library
endef

include $(MKFILES_ROOT)/qtargets.mk
