#version 330 core
layout (location = 0) in vec3 vPos;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec3 vColor;

// 인스턴싱: 물체마다 다른 값(모델 행렬, 재질)을 정점 속성으로 받는다 (glVertexAttribDivisor = 1)
layout (location = 3) in vec4 iM0;
layout (location = 4) in vec4 iM1;
layout (location = 5) in vec4 iM2;
layout (location = 6) in vec4 iM3;
layout (location = 7) in vec4 iColorEmis;   // rgb 색, a 자체발광
layout (location = 8) in vec4 iMat;         // ks, shininess, 텍스처 층, 텍스처 크기
layout (location = 9) in float iPattern;

uniform bool instanced;     // false 면 아래 uniform 으로 물체 하나를 그린다 (반투명, 손에 든 총)
uniform mat4 model;
uniform mat3 normalMat;
uniform vec3 objColor;
uniform float Ks;
uniform float shininess;
uniform float emissive;
uniform int pattern;
uniform int texLayer;
uniform float texScale;

uniform mat4 view;
uniform mat4 proj;
uniform mat4 lightVP;

out vec3 wPos;
out vec3 wNormal;
out vec3 vNormalView;
out vec4 lsPos;
out float viewDepth;
out vec3 oPos;              // 물체 공간 좌표(크기 반영) → 텍스처 좌표. 물체와 함께 움직이고 회전한다
out vec3 oNormal;
flat out vec3 fColor;
flat out float fKs, fShin, fEmis, fTexScale;
flat out int fTexLayer, fPattern;
flat out vec3 fObjScale;

void main()
{
    mat4 M;
    mat3 NM;
    if (instanced) {
        M = mat4(iM0, iM1, iM2, iM3);
        NM = transpose(inverse(mat3(M)));
        fColor = iColorEmis.rgb;
        fEmis = iColorEmis.a;
        fKs = iMat.x;
        fShin = iMat.y;
        fTexLayer = int(iMat.z);
        fTexScale = iMat.w;
        fPattern = int(iPattern);
    } else {
        M = model;
        NM = normalMat;
        fColor = objColor;
        fEmis = emissive;
        fKs = Ks;
        fShin = shininess;
        fTexLayer = texLayer;
        fTexScale = texScale;
        fPattern = pattern;
    }
    fObjScale = vec3(length(M[0].xyz), length(M[1].xyz), length(M[2].xyz));

    vec4 w = M * vec4(vPos, 1.0);
    wPos = w.xyz;
    wNormal = normalize(NM * vNormal);
    vNormalView = mat3(view) * wNormal;
    lsPos = lightVP * w;
    oPos = vPos * fObjScale;
    oNormal = vNormal;
    vec4 v = view * w;
    viewDepth = -v.z;
    gl_Position = proj * v;
}
