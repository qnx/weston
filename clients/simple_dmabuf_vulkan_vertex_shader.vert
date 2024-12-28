#version 450 core

layout(std140, set = 0, binding = 0) uniform block {
	uniform mat4 reflection;
	uniform float offset;
};

layout(location = 0) in vec4 pos;
layout(location = 1) in vec4 color;

layout(location = 0) out vec4 v_color;

void main()
{
	gl_Position = reflection * (pos + vec4(offset, offset, 0.0, 0.0));
	v_color = color;
}
