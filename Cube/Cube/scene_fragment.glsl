#version 330 core
in vec3 wPos;
in vec3 wNormal;
in vec3 vNormalView;
in vec4 lsPos;
in float viewDepth;
in vec3 oPos;
in vec3 oNormal;
in vec2 fUV;
in vec3 wRadial;
// 재질은 정점 셰이더가 넘겨준다 (인스턴스 속성 또는 uniform). 물체 하나 안에서는 같은 값이라 flat
flat in vec3 fColor;
flat in float fKs, fShin, fEmis, fTexScale;
flat in int fTexLayer, fPattern;
flat in vec3 fObjScale;
#define objColor fColor
#define Ks fKs
#define emissive fEmis
#define pattern fPattern            // 0 단색, 1 바닥 타일, 2 콘크리트 노이즈, 3 유리 격자 벽, 4 동심원으로 솔질한 금속,
                                    // 5 텍스처 모델(열차), 6 열차 실내 불빛
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
// 모델 텍스처 (pattern 5): 색, ORM(G 거칠기, B 금속), 노멀(OpenGL 방식)
uniform sampler2D modelColor, modelOrm, modelNormal;

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
    if (pattern == 4) {
        // 동심원 결과 옅은 얼룩. 멀어서 결이 픽셀보다 가늘어지면 지운다 (깜빡임 방지)
        vec2 q = oPos.xy / objScale.xy;
        float r = length(q) * 150.0;
        float fade = clamp(1.0 - fwidth(r), 0.0, 1.0);
        float ring = (noise(vec2(r, 0.5)) - 0.5) * 0.22 * fade;
        return base * (1.0 + ring - noise(q * 6.0) * 0.1);
    }
    return base;
}

// 솔질한 금속의 이방성 반사: 결 방향 T 와 하프 벡터가 수직일수록 밝다 → 부채꼴로 퍼지는 하이라이트
float brushedSpec(vec3 T, vec3 L, vec3 V, vec3 N)
{
    float th = dot(T, normalize(L + V));
    return pow(max(1.0 - th * th, 0.0), 45.0) * max(dot(N, L), 0.0);
}

// 노멀맵: 탄젠트가 없으므로 화면 미분으로 접선 공간을 만든다. glTF UV 는 v 가 아래로 자라 y 를 뒤집는다
vec3 modelNormalAt(vec3 N)
{
    vec3 dp1 = dFdx(wPos), dp2 = dFdy(wPos);
    vec2 du1 = dFdx(fUV), du2 = dFdy(fUV);
    vec3 dp2p = cross(dp2, N), dp1p = cross(N, dp1);
    vec3 T = dp2p * du1.x + dp1p * du2.x, B = dp2p * du1.y + dp1p * du2.y;
    float m = max(dot(T, T), dot(B, B));
    if (m < 1e-12) return N;
    vec3 t = texture(modelNormal, fUV).xyz * 2.0 - 1.0;
    return normalize((T * t.x - B * t.y) / sqrt(m) + N * t.z);
}

// 열차 실내: 천장 조명 띠는 밝고 아래로 갈수록 어둡다. 손잡이 기둥과 앉은 승객의 그림자가 비친다
float cabinGlow()
{
    float y = oPos.y / objScale.y + 0.5;        // 0 바닥 ~ 1 천장
    float u = abs(oNormal.x) > 0.5 ? oPos.z : oPos.x;
    float g = mix(0.3, 1.0, smoothstep(0.0, 0.85, y)) + 0.9 * smoothstep(0.84, 0.92, y);
    g *= 1.0 - 0.6 * (1.0 - smoothstep(0.02, 0.035, abs(fract(u / 0.9) - 0.5) * 0.9));
    float seat = floor(u / 0.45), hd = length(vec2((fract(u / 0.45) - 0.5) * 0.45, (y - 0.2) * objScale.y));
    if (hash(vec2(seat, oNormal.z)) > 0.55) g *= mix(0.25, 1.0, smoothstep(0.13, 0.16, hd));
    return g;
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
    float specMask = 1.0, shin = fShin, metalness = 0.0;
    bool useTex = texLayer >= 0 && style != ST_BLUEPRINT;
    if (pattern == 5) {
        vec3 orm = texture(modelOrm, fUV).rgb;
        N = modelNormalAt(N);
        base = texture(modelColor, fUV).rgb * objColor;
        metalness = orm.b;
        specMask = mix(0.35, 1.0, metalness) * (1.0 - 0.75 * orm.g);
        shin = mix(96.0, 10.0, orm.g);
    } else if (useTex) {
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
    if (style == ST_TOON && pattern != 5) base = mix(objColor, base, 0.3) * 1.1;            // 무늬는 약하게, 색은 선명하게
    if (style == ST_BLUEPRINT) base = vec3(0.75);
    float ks = Ks * specMask;

    float sh = shadowFactor(N, L);
    float NdotL = max(dot(N, L), 0.0);
    vec3 amb = mix(groundAmb, skyAmb, N.y * 0.5 + 0.5);     // 반구 주변광
    vec3 R = reflect(-L, N);
    float spec = NdotL > 0.0 ? pow(max(dot(V, R), 0.0), shin) * ks : 0.0;
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
        spec = NdotL > 0.0 ? pow(nh, shin * 2.0) * (shin + 8.0) / 25.0 * F : 0.0;
        amb *= 0.85 + 0.3 * F;
    }

    vec3 col = base * (amb + sunColor * diff) + sunColor * spec * sh;
    // 툰·청사진은 단순한 명암을 유지하려고 금속 반사를 뺀다
    bool brushed = pattern == 4 && style != ST_TOON && style != ST_BLUEPRINT && dot(wRadial, wRadial) > 1e-8;
    vec3 T = vec3(0.0), metal = mix(base, vec3(1.0), 0.25);
    if (brushed) {
        T = normalize(cross(N, wRadial));
        col += sunColor * metal * brushedSpec(T, L, V, N) * sh * 0.8;
        vec3 E = reflect(-V, N);        // 주변 반사: 위는 하늘색, 아래는 땅색
        col += mix(groundAmb, skyAmb, smoothstep(-0.2, 0.5, E.y)) * metal * (0.25 + 0.5 * fres);
    }
    col += skyAmb * fres * ks * (style == ST_REAL ? 0.9 : 0.4);

    if (pattern == 5) {      // 금속 몸체의 주변 반사 (환경맵 대신 하늘색·땅색)
        vec3 E = reflect(-V, N);
        col += mix(groundAmb, skyAmb, smoothstep(-0.3, 0.6, E.y)) * base * metalness * (0.5 + fres);
    }

    // 점광원: Phong(확산 + 반사) + 거리 감쇠. 범위 끝에서 부드럽게 0 이 되도록
    for (int i = 0; i < numLights; ++i) {
        vec3 lv = lightPos[i] - wPos;
        float d = length(lv);
        if (d > lightRange[i]) continue;
        vec3 Lp = lv / d;
        float fall = clamp(1.0 - pow(d / lightRange[i], 2.0), 0.0, 1.0);
        float att = fall * fall / (1.0 + 0.05 * d * d);
        float nl = max(dot(N, Lp), 0.0);
        float sp = nl > 0.0 ? pow(max(dot(V, reflect(-Lp, N)), 0.0), shin) * ks : 0.0;
        if (style == ST_TOON) { nl = band(nl * att * 3.0) / max(att * 3.0, 0.001); sp = step(0.5, sp) * 0.6; }
        col += (base * nl + sp) * lightCol[i] * att;
        if (brushed) col += metal * brushedSpec(T, Lp, V, N) * lightCol[i] * att * 0.6;
    }

    if (pointPower > 0.0) {
        vec3 lv = pointPos - wPos;
        float d = length(lv);
        float a = pointPower / (1.0 + 0.5 * d + 0.3 * d * d);
        col += base * pointColor * max(dot(N, lv / d), 0.0) * a;
    }

    float emis = emissive;
    if (pattern == 6) { emis *= cabinGlow(); col *= 0.1; }
    if (style == ST_NEON) {
        // 네온: 표면은 거의 검게, 물체 공간 격자선과 발광체만 빛난다 (높이에 따라 자홍 → 청록)
        col *= 0.12;
        vec3 an = abs(oNormal);
        vec2 q = an.y > 0.5 ? oPos.xz : (an.x > 0.5 ? oPos.zy : oPos.xy);
        vec2 g = abs(fract(q / 2.0) - 0.5);
        float line = smoothstep(0.46, 0.49, max(g.x, g.y));
        if (pattern == 4) line = smoothstep(0.95, 0.97, length(oPos.xy / objScale.xy));   // 방패는 테두리만 빛난다
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
