#version 330 core
in vec3 wPos;
in vec3 wNormal;
in vec3 vNormalView;
in vec4 lsPos;
in float viewDepth;
in vec3 oPos;
in vec3 oNormal;

layout (location = 0) out vec4 outColor;
layout (location = 1) out vec4 outNormal;   // 뷰 공간 법선 + 선형 깊이 (외곽선 검출용)

uniform vec3 objColor;
uniform float Ks;
uniform float shininess;
uniform float emissive;
uniform int pattern;                // 0 단색, 1 바닥 타일, 2 콘크리트 노이즈, 3 유리 격자 벽
uniform float alpha;                // 1 불투명, 1 미만은 반투명(포털 막)

uniform vec3 sunDir;                // 표면 → 태양 방향
uniform vec3 sunColor;
uniform vec3 skyAmb;
uniform vec3 groundAmb;
uniform vec3 viewPos;
uniform vec3 fogColor;
uniform float fogDensity;
uniform float farPlane;
uniform sampler2D shadowMap;

uniform vec3 pointPos;
uniform vec3 pointColor;
uniform float pointPower;

// 텍스처 매핑: 코드로 만든 이미지 배열. 물체 공간 좌표를 면 방향에 맞는 평면에 투영해 UV 로 쓴다
uniform sampler2DArray albedoTex;
uniform int texLayer;               // -1 이면 텍스처 없음
uniform float texScale;             // 텍스처 한 장이 덮는 길이
uniform vec3 objScale;

// 점광원(조명등, 네온) 여러 개. 카메라에서 가까운 것만 CPU 가 골라서 보낸다
const int MAX_LIGHTS = 24;
uniform int numLights;
uniform vec3 lightPos[MAX_LIGHTS];
uniform vec3 lightCol[MAX_LIGHTS];
uniform float lightRange[MAX_LIGHTS];

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

float noise(vec2 p)
{
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x),
               mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}

// 텍스처 이미지 없이 월드 좌표로 표면 무늬 생성
vec3 surface(vec3 base, vec3 N)
{
    if (pattern == 1) {
        // 방의 어느 면이든 바닥이 될 수 있으므로 법선에 맞는 평면으로 타일을 깐다
        vec2 q = abs(N.y) > 0.5 ? wPos.xz : (abs(N.x) > 0.5 ? wPos.zy : wPos.xy);
        vec2 g = q / 2.0;
        vec2 f = abs(fract(g) - 0.5);
        float line = smoothstep(0.475, 0.495, max(f.x, f.y));
        float n = noise(q * 1.7) * 0.08 + noise(q * 7.0) * 0.04;
        float tile = hash(floor(g)) * 0.06;
        return base * (1.0 - line * 0.3 - n - tile);
    }
    if (pattern == 2) {
        vec2 p = abs(N.y) > 0.5 ? wPos.xz : (abs(N.x) > 0.5 ? wPos.zy : wPos.xy);
        float n = noise(p * 2.3) * 0.07 + noise(p * 9.0) * 0.035;
        return base * (1.0 - n);
    }
    if (pattern == 3) {
        // 유리 격자: 큰 창틀(어두운 선) 안에 판유리마다 밝기가 조금씩 다르다
        vec2 q = abs(N.y) > 0.5 ? wPos.xz : (abs(N.x) > 0.5 ? wPos.zy : wPos.xy);
        vec2 g = q / 3.2;
        vec2 f = abs(fract(g) - 0.5);
        float mullion = smoothstep(0.43, 0.46, max(f.x, f.y));
        float big = step(0.47, max(abs(fract(q / 12.8).x - 0.5), abs(fract(q / 12.8).y - 0.5)));
        float pane = 0.82 + 0.28 * hash(floor(g)) + 0.1 * noise(q * 0.3);
        vec3 glass = base * pane;
        return mix(glass, vec3(0.16, 0.17, 0.2), max(mullion, big));
    }
    return base;
}

// PCF 3x3 그림자
float shadowFactor(vec3 N, vec3 L)
{
    vec3 p = lsPos.xyz / lsPos.w * 0.5 + 0.5;
    if (p.z > 1.0) return 1.0;
    float bias = max(0.002 * (1.0 - dot(N, L)), 0.0005);
    vec2 ts = 1.0 / vec2(textureSize(shadowMap, 0));
    float s = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y) {
            float d = texture(shadowMap, p.xy + vec2(x, y) * ts).r;
            s += (p.z - bias > d) ? 0.0 : 1.0;
        }
    return s / 9.0;
}

void main()
{
    vec3 N = normalize(wNormal);
    vec3 L = normalize(sunDir);
    vec3 V = normalize(viewPos - wPos);
    vec3 base;
    float specMask = 1.0;
    if (texLayer >= 0) {
        vec3 an = abs(oNormal);
        vec2 uv = an.y > 0.5 ? oPos.xz : (an.x > 0.5 ? oPos.zy : oPos.xy);
        vec2 sz = an.y > 0.5 ? objScale.xz : (an.x > 0.5 ? objScale.zy : objScale.xy);
        // texScale < 0: 면 하나에 텍스처 한 장을 딱 맞춤 (상자 테두리용)
        vec2 tc = texScale > 0.0 ? uv / texScale : uv / sz + 0.5;
        vec4 t = texture(albedoTex, vec3(tc, float(texLayer)));
        base = objColor * t.rgb;
        specMask = t.a;
    } else {
        base = surface(objColor, N);
    }
    float ks = Ks * specMask;

    float sh = shadowFactor(N, L);
    float NdotL = max(dot(N, L), 0.0);
    vec3 amb = mix(groundAmb, skyAmb, N.y * 0.5 + 0.5);     // 반구 주변광
    vec3 R = reflect(-L, N);
    float spec = NdotL > 0.0 ? pow(max(dot(V, R), 0.0), shininess) * ks : 0.0;

    vec3 col = base * (amb + sunColor * NdotL * sh) + sunColor * spec * sh;
    float fres = pow(1.0 - max(dot(N, V), 0.0), 4.0) * ks * 0.4;
    col += skyAmb * fres;

    // 점광원: Phong(확산 + 반사) + 거리 감쇠. 범위 끝에서 부드럽게 0 이 되도록
    for (int i = 0; i < numLights; ++i) {
        vec3 lv = lightPos[i] - wPos;
        float d = length(lv);
        if (d > lightRange[i]) continue;
        vec3 Lp = lv / d;
        float fall = clamp(1.0 - pow(d / lightRange[i], 2.0), 0.0, 1.0);
        float att = fall * fall / (1.0 + 0.05 * d * d);
        float nl = max(dot(N, Lp), 0.0);
        float sp = nl > 0.0 ? pow(max(dot(V, reflect(-Lp, N)), 0.0), shininess) * ks : 0.0;
        col += (base * nl + sp) * lightCol[i] * att;
    }

    if (pointPower > 0.0) {
        vec3 lv = pointPos - wPos;
        float d = length(lv);
        float a = pointPower / (1.0 + 0.5 * d + 0.3 * d * d);
        col += base * pointColor * max(dot(N, lv / d), 0.0) * a;
    }

    col += objColor * emissive;

    float fog = 1.0 - exp(-fogDensity * length(viewPos - wPos));
    col = mix(col, fogColor, fog);

    outColor = vec4(col, alpha);
    outNormal = vec4(normalize(vNormalView) * 0.5 + 0.5, clamp(viewDepth / farPlane, 0.0, 0.998));
}
