if not get_option('backend-drm')
	subdir_done()
endif

lib_backlight = static_library(
	'backlight',
	'libbacklight.c',
	dependencies: [
		dep_libdrm_headers,
		dependency('libudev')
	],
	include_directories: common_inc,
	install: false
)
dep_backlight = declare_dependency(
	link_with: lib_backlight,
	include_directories: include_directories('.')
)

config_h.set('BUILD_DRM_COMPOSITOR', '1')

srcs_drm = [
	'drm.c',
	'fb.c',
	'modes.c',
	'kms.c',
	'kms-color.c',
	'state-helpers.c',
	'state-propose.c',
	linux_dmabuf_unstable_v1_protocol_c,
	linux_dmabuf_unstable_v1_server_protocol_h,
	presentation_time_server_protocol_h,
]

deps_drm = [
	dep_egl, # optional
	dep_vulkan, # optional
	dep_libm,
	dep_libdl,
	dep_libshared,
	dep_libweston_private,
	dep_session_helper,
	dep_libdrm,
	dep_libinput_backend,
	dependency('libudev', version: '>= 136'),
	dep_libdisplay_info,
	dep_backlight
]

if get_option('renderer-gl')
	if not dep_gbm.found()
		error('drm-backend with GL renderer requires gbm which was not found. Or, you can use \'-Drenderer-gl=false\'.')
	endif
	deps_drm += dep_gbm
	srcs_drm += 'drm-gbm.c'
	config_h.set('BUILD_DRM_GBM', '1')
endif

if get_option('renderer-vulkan')
	if not dep_gbm.found()
		error('drm-backend with Vulkan renderer requires gbm which was not found. Or, you can use \'-Drenderer-vulkan=false\'.')
	endif
	deps_drm += dep_gbm
	srcs_drm += 'drm-gbm.c'
	config_h.set('BUILD_DRM_GBM', '1')
endif

if get_option('deprecated-remoting') or get_option('deprecated-pipewire')
	if not get_option('renderer-gl')
		error('DRM virtual requires renderer-gl.')
	endif
	srcs_drm += 'drm-virtual.c'
	config_h.set('BUILD_DRM_VIRTUAL', '1')
endif

plugin_drm = shared_library(
	'drm-backend',
	srcs_drm,
	include_directories: common_inc,
	dependencies: deps_drm,
	name_prefix: '',
	install: true,
	install_dir: dir_module_libweston
)
env_modmap += 'drm-backend.so=@0@;'.format(plugin_drm.full_path())

install_headers(backend_drm_h, subdir: dir_include_libweston_install)
