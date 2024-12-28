#version 450

layout(binding = 0) uniform _ubo {
	uniform mat4 proj;
	uniform mat4 surface_to_buffer;
} ubo;

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texcoord;

layout(location = 1) out vec2 v_texcoord;

void main() {
	gl_Position = ubo.proj * vec4(position, 0.0, 1.0);
	v_texcoord = texcoord;
}
