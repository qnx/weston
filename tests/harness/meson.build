plugin_test_shell_desktop = shared_library(
	'weston-test-desktop-shell',
	'weston-test-desktop-shell.c',
	include_directories: common_inc,
	dependencies: [ dep_libweston_public, dep_libexec_weston ],
	name_prefix: '',
	install: false
)
env_modmap += 'weston-test-desktop-shell.so=@0@;'.format(plugin_test_shell_desktop.full_path())

lib_test_runner = static_library(
	'test-runner',
	'weston-test-runner.c',
	dependencies: [
		dep_libweston_private_h_deps,
		dep_wayland_client,
                dep_libdl,
                dep_threads,
	],
	include_directories: common_inc,
	install: false,
)
dep_test_runner = declare_dependency(
	dependencies: dep_wayland_client,
	link_with: lib_test_runner
)

lib_test_client = static_library(
	'test-client',
	[
		'weston-test-client-helper.c',
		'weston-test-fixture-compositor.c',
		'xdg-client-helper.c',
		color_representation_v1_client_protocol_h,
		color_representation_v1_protocol_c,
		commit_timing_v1_client_protocol_h,
		commit_timing_v1_protocol_c,
		fifo_v1_client_protocol_h,
		fifo_v1_protocol_c,
		presentation_time_client_protocol_h,
		presentation_time_protocol_c,
		single_pixel_buffer_v1_client_protocol_h,
		single_pixel_buffer_v1_protocol_c,
		weston_test_client_protocol_h,
		weston_test_protocol_c,
		weston_output_capture_client_protocol_h,
		weston_output_capture_protocol_c,
		viewporter_client_protocol_h,
		viewporter_protocol_c,
		xdg_shell_client_protocol_h,
		xdg_shell_protocol_c,
		'color_util.h',
		'color_util.c',
	],
	include_directories: common_inc,
	dependencies: [
		dep_client_buffer,
		dep_libshared,
		dep_wayland_client,
		dep_libexec_weston,
		dep_libweston_private,
		dep_libdrm_headers,
		dep_pixman,
                dep_threads,
		dependency('cairo'),
	],
	install: false,
)
dep_test_client = declare_dependency(
	link_with: lib_test_client,
	sources: [
		weston_test_client_protocol_h,
		viewporter_client_protocol_h,
	],
	dependencies: [
		dep_wayland_client,
		dep_test_runner,
		dep_pixman,
		dep_libdrm_headers,
                dep_threads,
		dependency('libudev', version: '>= 136'),
	],
	include_directories: include_directories('.'),
)

lib_lcms_util = static_library(
	'lib_lcms_util',
	[ 'lcms_util.c' ],
	include_directories: common_inc,
	dependencies: [
		dep_lcms2, dep_libm, dep_wayland_server, dep_pixman
	],
	build_by_default: false,
	install: false,
)
dep_lcms_util = declare_dependency(
	link_with: lib_lcms_util,
	dependencies: [ dep_lcms2 ]
)

exe_plugin_test = shared_library(
	'test-plugin',
	'weston-test.c',
	weston_test_server_protocol_h,
	weston_test_protocol_c,
	include_directories: common_inc,
	dependencies: [
		dep_libexec_weston,
		dep_libweston_private,
		dep_threads
	],
	name_prefix: '',
	install: false,
)

if get_option('xwayland')
	xcb_dep = dependency('xcb', required: false)
	xcb_cursor_dep = dependency('xcb-cursor', required: false)

	if not xcb_dep.found() or not xcb_cursor_dep.found()
		error('xcb and xcb-cursor required for running xwayland tests')
	endif

	libxwayland_test_client = static_library(
		'test-xwayland-client',
		'xcb-client-helper.c',
		include_directories: common_inc,
		dependencies: [
			dep_pixman, dep_xcb_xwayland,
			xcb_dep, xcb_cursor_dep,
			dep_wayland_client,
		],
		install: false,
	)

	dep_libxwayland_test = declare_dependency(
		dependencies: [ xcb_dep, xcb_cursor_dep ],
		link_with: libxwayland_test_client,
	)
else
	dep_libxwayland_test = disabler()
endif
