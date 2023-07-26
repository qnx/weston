# Weston on QNX
See top-level project README.md for the most accurate and up-to-date information on Weston and the Wayland project.

For QNX, a backend for the QNX Screen graphics subsystem has been developed. See `libweston/backend-qnxscreen` for the implementation. This backend should be considered as a work-in-progress. There may be some features/functionality that still need to be implemented or improved.


## Dependencies

The following packages include development packages, which should be installed on the host, and runtime packages, which should be installed on the target. These are required to build and run Weston on QNX. `${baselineId}` refers to the QNX SDP version to target, i.e. qnx800:
- com.qnx.${baselineId}.osr.libffi
- com.qnx.${baselineId}.osr.pixman
- com.qnx.${baselineId}.osr.cairo
- com.qnx.${baselineId}.osr.libffi
- com.qnx.${baselineId}.target.screen.wayland
- com.qnx.${baselineId}.target.screen.fonts.noto
    - Or some other fonts package, such as fonts.dejavu

## Building

**NOTE**: QNX ports are only tested from a Linux host operating system

- Setup your QNX SDP environment
- From the root folder:
	- `OSLIST=nto make -C qnx/build install`

This build structure uses the standard QNX recursive Makefile pattern.

## Running
### Deployment
The following assumes that all dependency packages (from SDP) **and** all components built as part of this project are installed to the same location on the host machine. This location is referred to as `$INSTALL_DIR`. Adjust process accordingly if components built from this project are installed elsewhere.

Usually, in the default QNX host machine configuration, `$INSTALL_DIR`=`$QNX_TARGET`. `$PROCESSOR` indicates the target-specific install folder (i.e. aarch64le or x86_64).

Install/mount the following SDP and/or weston-built components on the target:


Weston:
- `$INSTALL_DIR`/etc/xdg
    - --> /etc/xdg
- `$INSTALL_DIR`/usr/share/weston
    - --> /usr/share/weston
- `$INSTALL_DIR`/`$PROCESSOR`/usr/lib/libweston
    - --> /usr/lib/libweston/
- `$INSTALL_DIR`/`$PROCESSOR`/usr/lib/weston
    - --> /usr/lib/weston/
- `$INSTALL_DIR`/`$PROCESSOR`/usr/libexec/weston-*
    - --> /usr/libexec/
- `$INSTALL_DIR`/`$PROCESSOR`/usr/bin/weston-*
    - --> /usr/bin/
- `$INSTALL_DIR`/`$PROCESSOR`/usr/lib/libweston*.so*
    - --> /usr/lib/

Wayland:
- `$INSTALL_DIR`/usr/share/keyboard
    - -->  /usr/share/keyboard
- `$INSTALL_DIR`/usr/share/xkb
    - -->  /usr/share/xkb
- `$INSTALL_DIR`/usr/share/locale
    - --> /usr/share/locale
- `$INSTALL_DIR`/`$PROCESSOR`/usr/lib/libwayland*.so*
- `$INSTALL_DIR`/`$PROCESSOR`/usr/lib/libepoll*
- `$INSTALL_DIR`/`$PROCESSOR`/usr/lib/libtimerfd*
- `$INSTALL_DIR`/`$PROCESSOR`/usr/lib/libmemstream*
- `$INSTALL_DIR`/`$PROCESSOR`/usr/lib/libeventfd*
- `$INSTALL_DIR`/`$PROCESSOR`/usr/lib/libxkbcommon*

Other Dependencies:
- `$INSTALL_DIR`/etc/fontconfig
    - --> /etc/fontconfig
- `$INSTALL_DIR`/usr/share/fonts
    - --> /usr/share/fonts
- `$INSTALL_DIR`/`$PROCESSOR`/lib/libpng16*
- `$INSTALL_DIR`/`$PROCESSOR`/lib/libxml2*
- `$INSTALL_DIR`/`$PROCESSOR`/lib/libjpeg*
- `$INSTALL_DIR`/`$PROCESSOR`/usr/lib/libcairo*
- `$INSTALL_DIR`/`$PROCESSOR`/usr/lib/libffi*
- `$INSTALL_DIR`/`$PROCESSOR`/usr/lib/libfontconfig*
- `$INSTALL_DIR`/`$PROCESSOR`/usr/lib/libfreetype*
- `$INSTALL_DIR`/`$PROCESSOR`/usr/lib/libiconv*
- `$INSTALL_DIR`/`$PROCESSOR`/usr/lib/liblzma*
- `$INSTALL_DIR`/`$PROCESSOR`/usr/lib/libpixman*

There may be other implicit dependencies missing from this list, which are assumed by any number of these shared libraries. If they are missing on the target installation, it should become apparent when attempting to run weston executables.

### Run on Target
> Note: QNX Screen must already be running and have at least one display available.

Create an XDG runtime directory on the target:
```
mkdir -p /run/user/$(id -u ${USER})
chmod 0700 /run/user/$(id -u ${USER})
```
> Note: XDG runtime directory, and TMPDIR, should be normal file systems with read/write access.

Start a desktop Weston:

```
export TMPDIR=/var/tmp
export XDG_RUNTIME_DIR=/run/user/$(id -u ${USER})

weston --fullscreen
```

For IVI Weston, use this command instead:
```
weston --fullscreen --config weston-ivi.ini
```

Run weston client application examples:
```
weston-clickdot &
weston-smoke &
```
