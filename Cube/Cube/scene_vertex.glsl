#version 330 core
layout (location = 0) in vec3 vPos;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec3 vColor;

uniform mat4 model;
uniform mat4 view;
uniform mat4 proj;
uniform mat4 lightVP;
uniform mat3 normalMat;
uniform vec3 objScale;      // 모델 행렬의 축별 크기. 텍스처가 물체 크기에 맞게 반복되도록

out vec3 wPos;
out vec3 wNormal;
out vec3 vNormalView;
out vec4 lsPos;
out float viewDepth;
out vec3 oPos;              // 물체 공간 좌표(크기 반영) → 텍스처 좌표. 물체와 함께 움직이고 회전한다
out vec3 oNormal;

void main()
{
    vec4 w = model * vec4(vPos, 1.0);
    wPos = w.xyz;
    wNormal = normalize(normalMat * vNormal);
    vNormalView = mat3(view) * wNormal;
    lsPos = lightVP * w;
    oPos = vPos * objScale;
    oNormal = vNormal;
    vec4 v = view * w;
    viewDepth = -v.z;
    gl_Position = proj * v;
}
