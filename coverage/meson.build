prog_gcovr = find_program('gcovr', required: false, disabler: true)

# Configure the build:
# $ meson configure -Db_sanitize=none -Db_coverage=true -Dwerror=true
#
# Ensure there are no stale .gcno or .gcda files around:
# $ meson setup --wipe ~/git/weston
#
# Run the test suite:
# $ meson compile
# $ meson test
#
# Generate the reports:
# $ meson compile gcovr-report

run_target(
	'gcovr-report',
	command: [
		prog_gcovr,
		'--root', '@SOURCE_ROOT@',
		'--cobertura', meson.current_build_dir() / 'cobertura.xml',
		'--html', meson.current_build_dir() / 'index.html',
		'--html-nested',
		'--html-theme', 'github.green',
		'--html-title', 'Weston test suite coverage',
		'--print-summary',
		meson.project_build_root(),
	],
)
