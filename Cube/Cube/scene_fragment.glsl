#version 330 core
in vec3 wPos;
in vec3 wNormal;
in vec3 vNormalView;
in vec4 lsPos;
in float viewDepth;
in vec3 oPos;
in vec3 oNormal;
// 재질은 정점 셰이더가 넘겨준다 (인스턴스 속성 또는 uniform). 물체 하나 안에서는 같은 값이라 flat
flat in vec3 fColor;
flat in float fKs, fShin, fEmis, fTexScale;
flat in int fTexLayer, fPattern;
flat in vec3 fObjScale;
#define objColor fColor
#define Ks fKs
#define shininess fShin
#define emissive fEmis
#define pattern fPattern            // 0 단색, 1 바닥 타일, 2 콘크리트 노이즈, 3 유리 격자 벽, 4 가스 행성, 5 지구형 행성, 6 바위 행성
#define texLayer fTexLayer          // -1 이면 텍스처 없음
#define texScale fTexScale          // 텍스처 한 장이 덮는 길이 (음수면 면 하나에 한 장)
#define objScale fObjScale

layout (location = 0) out vec4 outColor;
layout (location = 1) out vec4 outNormal;   // 뷰 공간 법선 + 선형 깊이 (외곽선 검출용)

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
    // 행성 무늬 (물체 좌표 = 단위 구 위의 점이라 행성과 함께 돈다)
    vec3 q = normalize(oPos);
    if (pattern == 4) {
        // 가스 행성: 위도 방향 줄무늬가 노이즈로 일렁인다
        float w = q.y * 9.0 + (noise(q.xz * 3.0 + 2.0) - 0.5) * 1.6;
        float b = sin(w * 2.0) * 0.5 + 0.5, b2 = sin(w * 5.3 + 1.0) * 0.5 + 0.5;
        return base * (0.62 + 0.32 * b + 0.12 * b2) * mix(vec3(1.0), vec3(1.1, 0.85, 0.7), b2 * 0.5);
    }
    if (pattern == 5) {
        // 지구형: 바다(재질 색) + 대륙 + 극지방 얼음
        float n = noise(q.xy * 2.1 + q.z * 1.3) * 0.55 + noise(q.yz * 4.7 + 3.0) * 0.3 + noise(q.zx * 11.0) * 0.15;
        vec3 land = mix(vec3(0.2, 0.42, 0.16), vec3(0.58, 0.48, 0.3), noise(q.xy * 6.0 + 9.0));
        vec3 c = mix(base, land, smoothstep(0.5, 0.53, n));
        return mix(c, vec3(0.95), smoothstep(0.88, 0.92, abs(q.y) + (n - 0.5) * 0.15));
    }
    if (pattern == 6) {
        // 바위 행성: 여러 크기 노이즈 + 어두운 크레이터 점
        float n = noise(q.xy * 4.0 + q.z * 2.0) * 0.5 + noise(q.yz * 9.0) * 0.3 + noise(q.zx * 21.0) * 0.2;
        float crater = smoothstep(0.78, 0.86, noise(q.xz * 7.0 + q.y * 3.0));
        return base * (0.6 + 0.6 * n) * (1.0 - crater * 0.35);
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

// 방마다 다른 렌더 스타일 (render.cpp 의 RenderStyle 과 같은 번호)
uniform int style;
const int ST_REAL = 0, ST_MINIMAL = 1, ST_TOON = 2, ST_PIXEL = 3, ST_NOIR = 4, ST_NEON = 5, ST_BLUEPRINT = 6;

// 툰: 빛을 3단계로 끊는다
float band(float x) { return x > 0.55 ? 1.0 : (x > 0.18 ? 0.55 : 0.18); }

void main()
{
    vec3 N = normalize(wNormal);
    vec3 L = normalize(sunDir);
    vec3 V = normalize(viewPos - wPos);
    vec3 base;
    float specMask = 1.0;
    bool useTex = texLayer >= 0 && style != ST_BLUEPRINT;
    if (useTex) {
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
    if (style == ST_TOON) base = mix(objColor, base, 0.3) * 1.1;            // 무늬는 약하게, 색은 선명하게
    if (style == ST_BLUEPRINT) base = vec3(0.75);
    float ks = Ks * specMask;

    float sh = shadowFactor(N, L);
    float NdotL = max(dot(N, L), 0.0);
    vec3 amb = mix(groundAmb, skyAmb, N.y * 0.5 + 0.5);     // 반구 주변광
    vec3 R = reflect(-L, N);
    float spec = NdotL > 0.0 ? pow(max(dot(V, R), 0.0), shininess) * ks : 0.0;
    float diff = NdotL * sh;
    float fres = pow(1.0 - max(dot(N, V), 0.0), 5.0);

    if (style == ST_TOON) {
        diff = band(diff);
        spec = step(0.5, spec) * 0.6;
    }
    if (style == ST_REAL) {
        // 실사풍: 슐릭 프레넬로 스치는 각도에서 반사가 강해지고, 반사광은 에너지 보존 쪽으로 정규화
        float f0 = mix(0.04, 0.6, clamp(ks, 0.0, 1.0));
        float F = f0 + (1.0 - f0) * fres;
        vec3 H = normalize(L + V);
        float nh = max(dot(N, H), 0.0);
        spec = NdotL > 0.0 ? pow(nh, shininess * 2.0) * (shininess + 8.0) / 25.0 * F : 0.0;
        amb *= 0.85 + 0.3 * F;
    }

    vec3 col = base * (amb + sunColor * diff) + sunColor * spec * sh;
    col += skyAmb * fres * ks * (style == ST_REAL ? 0.9 : 0.4);

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
        if (style == ST_TOON) { nl = band(nl * att * 3.0) / max(att * 3.0, 0.001); sp = step(0.5, sp) * 0.6; }
        col += (base * nl + sp) * lightCol[i] * att;
    }

    if (pointPower > 0.0) {
        vec3 lv = pointPos - wPos;
        float d = length(lv);
        float a = pointPower / (1.0 + 0.5 * d + 0.3 * d * d);
        col += base * pointColor * max(dot(N, lv / d), 0.0) * a;
    }

    float emis = emissive;
    if (style == ST_NEON) {
        // 네온: 표면은 거의 검게, 물체 공간 격자선과 발광체만 빛난다 (높이에 따라 자홍 → 청록)
        col *= 0.12;
        vec3 an = abs(oNormal);
        vec2 q = an.y > 0.5 ? oPos.xz : (an.x > 0.5 ? oPos.zy : oPos.xy);
        vec2 g = abs(fract(q / 2.0) - 0.5);
        float line = smoothstep(0.46, 0.49, max(g.x, g.y));
        vec3 neon = mix(vec3(1.0, 0.15, 0.85), vec3(0.1, 0.95, 1.0), clamp(wPos.y / 40.0, 0.0, 1.0));
        col += neon * line * 1.4;
        emis *= 2.5;
    }
    col += objColor * emis;

    float fogD = fogDensity * (style == ST_NOIR ? 1.6 : (style == ST_NEON ? 0.5 : 1.0));
    vec3 fogC = style == ST_NEON ? vec3(0.02, 0.0, 0.05) : fogColor;
    float fog = 1.0 - exp(-fogD * length(viewPos - wPos));
    col = mix(col, fogC, fog);

    outColor = vec4(col, alpha);
    outNormal = vec4(normalize(vNormalView) * 0.5 + 0.5, clamp(viewDepth / farPlane, 0.0, 0.998));
}
