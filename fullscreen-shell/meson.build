if get_option('deprecated-shell-fullscreen')
        warning('deprecated-shell-fullscreen enabled. This will go away, see https://gitlab.freedesktop.org/wayland/weston/-/issues/848')

	srcs_shell_fullscreen = [
		'fullscreen-shell.c',
		fullscreen_shell_unstable_v1_server_protocol_h,
		fullscreen_shell_unstable_v1_protocol_c,
	]
	deps_shell_fullscreen = [
		dep_libweston_public,
		dep_libexec_weston,
	]
	shared_library(
		'fullscreen-shell',
		srcs_shell_fullscreen,
		include_directories: common_inc,
		dependencies: deps_shell_fullscreen,
		name_prefix: '',
		install: true,
		install_dir: dir_module_weston
	)
endif
