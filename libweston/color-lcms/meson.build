build_color_lcms = get_option('color-management-lcms')
config_h.set10('BUILD_COLOR_LCMS', build_color_lcms)

if not build_color_lcms
	subdir_done()
endif

if not dep_lcms2.found()
	error('color-lcms plugin requires lcms2 which was not found. Or, you can use \'-Dcolor-management-lcms=false\'.')
endif

has_function_cmsGetToneCurveSegment = cc.has_function('cmsGetToneCurveSegment',
						      dependencies: dep_lcms2)

config_h.set10('HAVE_CMS_GET_TONE_CURVE_SEGMENT',
	       has_function_cmsGetToneCurveSegment)

srcs_color_lcms = [
	color_management_v1_server_protocol_h,
	'color-lcms.c',
	'color-profile.c',
	'color-transform.c',
]
if (has_function_cmsGetToneCurveSegment)
	srcs_color_lcms += 'color-curve-segments.c'
endif

deps_color_lcms = [
	dep_libm,
	dep_libweston_private,
	dep_lcms2,
	dep_libshared,
]

plugin_color_lcms = shared_library(
	'color-lcms',
	srcs_color_lcms,
	include_directories: common_inc,
	dependencies: deps_color_lcms,
	name_prefix: '',
	install: true,
	install_dir: dir_module_libweston
)
env_modmap += 'color-lcms.so=@0@;'.format(plugin_color_lcms.full_path())
