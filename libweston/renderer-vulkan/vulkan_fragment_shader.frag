#version 450

layout(binding = 1) uniform _ubo {
	uniform vec4 unicolor;
	uniform float view_alpha;
} ubo;
layout(binding = 2) uniform sampler2D tex;

layout(location = 1) in vec2 v_texcoord;

layout(location = 0) out vec4 fragcolor;

layout(constant_id = 0) const int c_variant = 0;
layout(constant_id = 1) const bool c_input_is_premult = false;

#define PIPELINE_VARIANT_RGBA     1
#define PIPELINE_VARIANT_RGBX     2
#define PIPELINE_VARIANT_SOLID    3
#define PIPELINE_VARIANT_EXTERNAL 4

vec4
sample_input_texture()
{
	if (c_variant == PIPELINE_VARIANT_SOLID)
		return ubo.unicolor;

	if (c_variant == PIPELINE_VARIANT_EXTERNAL ||
	    c_variant == PIPELINE_VARIANT_RGBA ||
	    c_variant == PIPELINE_VARIANT_RGBX) {
		vec4 color;

		color = texture(tex, v_texcoord);

		if (c_variant == PIPELINE_VARIANT_RGBX)
			color.a = 1.0;

		return color;
	}

	/* Never reached, bad variant value. */
	return vec4(1.0, 0.3, 1.0, 1.0);
}

void main() {
	vec4 color;

	/* Electrical (non-linear) RGBA values, may be premult or not */
	color = sample_input_texture();

	/* Ensure pre-multiplied for blending */
	if (!c_input_is_premult)
		color.rgb *= color.a;

	color *= ubo.view_alpha;

	fragcolor = color;
}
