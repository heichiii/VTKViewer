#version 430 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in float aScalar;

uniform mat4 mvp;

void main()
{
    gl_Position = mvp * vec4(aPosition, 1.0);
}
