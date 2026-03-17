#!/bin/bash
set -xe

source "${FDO_CI_BASH_HELPERS}"

cd "$BUILDDIR"
test -n "${QEMU_SMP}" || QEMU_SMP=${FDO_CI_CONCURRENT:-4}
virtme-run --rw --pwd --kimg /weston-virtme/${KERNEL_IMAGE} --kopt quiet --kopt log_buf_len=2M --script-sh ../.gitlab-ci/virtme-scripts/run-weston-tests.sh --qemu-opts -m 4096 -smp ${QEMU_SMP}
TEST_RES=$(cat $TESTS_RES_PATH)
rm $TESTS_RES_PATH
cp -R /weston-virtme ./
rm weston-virtme/${KERNEL_IMAGE}
exit $TEST_RES
