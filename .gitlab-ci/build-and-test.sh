#!/bin/bash

source "${FDO_CI_BASH_HELPERS}"

fdo_log_section_start_collapsed build_weston "build_weston"
cd "$BUILDDIR"
meson --prefix="$PREFIX" --wrap-mode=nofallback -Db_sanitize=address ${MESON_OPTIONS} ${MESON_TOOLCHAIN_OPTIONS} ${MESON_DIST_OPTIONS} ..
ninja -k0 -j${FDO_CI_CONCURRENT:-4}
ninja install

if [ "$CI_JOB_NAME" == "x86_64-debian-full-build" ]; then
	cd "$BUILDDIR_WESTINY"
	export NPREFIX=$CI_PROJECT_DIR/prefix-weston-$CI_JOB_NAME
	export PKG_CONFIG_PATH=$NPREFIX/lib/pkgconfig/:$NPREFIX/share/pkgconfig/:$NPREFIX/lib/x86_64-linux-gnu/pkgconfig:$PKG_CONFIG_PATH
	meson setup --prefix="$PREFIX_WESTINY" --wrap-mode=nofallback ../westinyplus/
	ninja -k0 -j${FDO_CI_CONCURRENT:-4}
	ninja install
	ninja clean
	cd -
fi
fdo_log_section_end build_weston

test -n "${QEMU_SMP}" || QEMU_SMP=${FDO_CI_CONCURRENT:-4}
virtme-run --rw --pwd --kimg /weston-virtme/${KERNEL_IMAGE} --kopt quiet --kopt log_buf_len=2M --script-sh ../.gitlab-ci/virtme-scripts/run-weston-tests.sh --qemu-opts -m 4096 -smp ${QEMU_SMP}
TEST_RES=$(cat $TESTS_RES_PATH)
rm $TESTS_RES_PATH
cp -R /weston-virtme ./
rm weston-virtme/${KERNEL_IMAGE}
exit $TEST_RES
