#version 430 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in float aScalar;

uniform mat4 mvp;
uniform mat4 modelView;
uniform mat3 normalMatrix;
uniform float pointSize = 5.0;

out vec3 vNormal;
out vec3 vPosition;
out float vScalar;

void main()
{
    vec4 viewPos = modelView * vec4(aPosition, 1.0);
    vPosition = viewPos.xyz;
    vNormal = normalize(normalMatrix * aNormal);
    vScalar = aScalar;
    
    gl_Position = mvp * vec4(aPosition, 1.0);
    gl_PointSize = pointSize;
}
