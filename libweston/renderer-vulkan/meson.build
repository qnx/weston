if not get_option('renderer-vulkan')
	subdir_done()
endif

config_h.set('ENABLE_VULKAN', '1')

srcs_shaders_renderer_vulkan = [
	'vulkan_vertex_shader_surface.vert',
	'vulkan_vertex_shader_texcoord.vert',
	'vulkan_fragment_shader.frag',
]

shaders_renderer_vulkan = []
foreach s : srcs_shaders_renderer_vulkan
	shaders_renderer_vulkan += custom_target(s + '.spv.h',
		command: [ prog_glslang, '@INPUT@', '--quiet', '--variable-name', '@BASENAME@', '-V', '-x', '-o', '@OUTPUT@' ],
		input: s,
		output: '@BASENAME@.spv.h',
	)
endforeach

srcs_renderer_vulkan = [
	'vulkan-pipeline.c',
	'vulkan-pixel-format.c',
	'vulkan-renderer.c',
	shaders_renderer_vulkan,
	linux_dmabuf_unstable_v1_protocol_c,
	linux_dmabuf_unstable_v1_server_protocol_h,
]

deps_renderer_vulkan = [
	dep_libdrm,

	dep_gbm,
	dep_libm,
	dep_pixman,
	dep_libweston_private,
	dep_libdrm_headers,
	dep_vertex_clipping
]

foreach name : [ 'vulkan' ]
	d = dependency(name, required: false)
	if not d.found()
		error('vulkan-renderer requires @0@ which was not found. Or, you can use \'-Drenderer-vulkan=false\'.'.format(name))
	endif
	deps_renderer_vulkan += d
endforeach

plugin_vulkan = shared_library(
	'vulkan-renderer',
	srcs_renderer_vulkan,
	include_directories: common_inc,
	dependencies: deps_renderer_vulkan,
	name_prefix: '',
	install: true,
	install_dir: dir_module_libweston
)
env_modmap += 'vulkan-renderer.so=@0@;'.format(plugin_vulkan.full_path())
