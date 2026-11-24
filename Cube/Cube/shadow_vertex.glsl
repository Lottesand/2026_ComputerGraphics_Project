#version 330 core
layout (location = 0) in vec3 vPos;
layout (location = 3) in vec4 iM0;
layout (location = 4) in vec4 iM1;
layout (location = 5) in vec4 iM2;
layout (location = 6) in vec4 iM3;
uniform bool instanced;
uniform mat4 lightVP;
uniform mat4 model;
void main()
{
    mat4 M = instanced ? mat4(iM0, iM1, iM2, iM3) : model;
    gl_Position = lightVP * M * vec4(vPos, 1.0);
}
