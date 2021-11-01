if not get_option('shell-lua')
	subdir_done()
endif

config_h.set('BUILD_LUA_SHELL', '1')

dep_lua = dependency('lua', version: '>= 5.4', required: false)
if not dep_lua.found()
	error('lua-shell requires Lua >= 5.4 which was not found. Or, you can use \'-Dshell-lua=false\'.')
endif

srcs_shell_lua = [
	'lua-shell.c',
]
deps_shell_lua = [
	dep_libm,
	dep_libexec_weston,
	dep_libshared,
	dep_libweston_public,
	dep_lua,
]
plugin_shell_lua = shared_library(
	'lua-shell',
	srcs_shell_lua,
	include_directories: common_inc,
	dependencies: deps_shell_lua,
	name_prefix: '',
	install: true,
	install_dir: dir_module_weston,
	install_rpath: '$ORIGIN'
)
env_modmap += 'lua-shell.so=@0@;'.format(plugin_shell_lua.full_path())
install_data('shell.lua', install_dir: get_option('libexecdir'))
