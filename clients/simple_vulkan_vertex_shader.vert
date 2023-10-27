#version 450 core

layout(std140, set = 0, binding = 0) uniform block {
	uniform mat4 rotation;
};

layout(location = 0) in vec4 in_position;
layout(location = 1) in vec4 in_color;

layout(location = 0) out vec4 vVaryingColor;

void main()
{
	gl_Position = rotation * in_position;
	vVaryingColor = vec4(in_color.rgba);
}
