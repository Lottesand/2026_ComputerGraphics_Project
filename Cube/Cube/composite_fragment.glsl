#version 330 core
in vec2 uv;
out vec4 FragColor;

uniform sampler2D sceneTex;
uniform sampler2D normalTex;
uniform sampler2D bloomTex;
uniform vec2 texel;
uniform float focus;     // 0~1 불릿타임 정도
uniform float hurt;      // 0~1 피격
uniform float time;
uniform int style;       // 방마다 다른 렌더 스타일 (render.cpp 의 RenderStyle)
uniform vec3 fadeColor;  // 방을 넘어갈 때 화면을 덮는 색 (구멍 속 빛)
uniform float fade;

const int ST_REAL = 0, ST_MINIMAL = 1, ST_TOON = 2, ST_PIXEL = 3, ST_NOIR = 4, ST_NEON = 5, ST_BLUEPRINT = 6;

vec2 suv;           // 샘플 위치 (픽셀 스타일이면 큰 픽셀 격자에 맞춘다)
float edgeW = 1.0;  // 외곽선 굵기

vec4 S(vec2 o) { return texture(normalTex, suv + o * texel * edgeW); }

vec3 aces(vec3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

// 4x4 베이어 행렬 (순서 디더링)
float bayer4(vec2 p)
{
    ivec2 i = ivec2(mod(p, 4.0));
    int m[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);
    return float(m[i.y * 4 + i.x]) / 16.0 - 0.5;
}

void main()
{
    suv = uv;
    float pixel = 4.0;
    if (style == ST_PIXEL) suv = (floor(uv / (texel * pixel)) + 0.5) * texel * pixel;
    if (style == ST_TOON) edgeW = 1.8;
    if (style == ST_NOIR) edgeW = 1.5;

    vec2 dir = suv - 0.5;
    float ca = focus * 0.004 + hurt * 0.008 + (style == ST_REAL ? 0.0015 : 0.0);
    vec3 col;
    col.r = texture(sceneTex, suv + dir * ca).r;
    col.g = texture(sceneTex, suv).g;
    col.b = texture(sceneTex, suv - dir * ca).b;

    // 외곽선: 법선은 Sobel, 깊이는 라플라시안(평면에서는 0)
    vec4 c = S(vec2(0.0));
    vec4 gx = -S(vec2(-1, -1)) - 2.0 * S(vec2(-1, 0)) - S(vec2(-1, 1)) + S(vec2(1, -1)) + 2.0 * S(vec2(1, 0)) + S(vec2(1, 1));
    vec4 gy = -S(vec2(-1, -1)) - 2.0 * S(vec2(0, -1)) - S(vec2(1, -1)) + S(vec2(-1, 1)) + 2.0 * S(vec2(0, 1)) + S(vec2(1, 1));
    float lap = S(vec2(1, 0)).a + S(vec2(-1, 0)).a + S(vec2(0, 1)).a + S(vec2(0, -1)).a - 4.0 * c.a;
    float eN = length(vec2(length(gx.xyz), length(gy.xyz)));
    float eD = abs(lap) / max(c.a, 0.002);
    float edge = max(smoothstep(0.6, 1.2, eN), smoothstep(0.03, 0.08, eD));
    edge *= 1.0 - smoothstep(0.3, 0.7, c.a);
    vec3 bloom = texture(bloomTex, suv).rgb;

    if (style == ST_REAL) {
        // 실사: 외곽선 없음, 블룸 강하게, 필름 톤 + 어두운 곳은 청록·밝은 곳은 따뜻하게
        col += bloom * 1.25;
        col = aces(col * 0.95);
        col = smoothstep(0.02, 1.0, col);       // 대비를 조금 올려 사진 같은 깊이감
        float l = dot(col, vec3(0.299, 0.587, 0.114));
        col *= mix(vec3(0.92, 1.0, 1.06), vec3(1.05, 1.0, 0.94), smoothstep(0.2, 0.8, l));
    } else if (style == ST_TOON) {
        col = mix(col, vec3(0.02), edge);
        col += bloom * 0.5;
        col = aces(col * 1.1);
        float l = dot(col, vec3(0.299, 0.587, 0.114));
        col = mix(vec3(l), col, 1.35);       // 채도 올림
    } else if (style == ST_PIXEL) {
        col += bloom * 0.6;
        col = aces(col * 1.05);
        // 채널마다 6단계로 줄이고 베이어 디더링
        float levels = 8.0;
        col = floor(col * levels + 0.5 + bayer4(floor(uv / (texel * pixel))) * 0.45) / levels;
    } else if (style == ST_NOIR) {
        col += bloom * 0.7;
        col = aces(col * 1.1);
        float l = dot(col, vec3(0.299, 0.587, 0.114));
        float ink = smoothstep(0.1, 0.75, l);
        bool red = col.r > 0.35 && col.r > col.g * 1.8 && col.r > col.b * 1.8;   // 붉은 것만 색을 남긴다
        col = red ? vec3(ink * 1.1, ink * 0.15, ink * 0.12) : vec3(ink);
        col = mix(col, vec3(0.0), edge);
        col *= 0.92 + 0.16 * hash(floor(uv / texel / 2.0) + floor(time * 12.0));   // 필름 입자
    } else if (style == ST_NEON) {
        vec3 glowC = mix(vec3(1.0, 0.2, 0.9), vec3(0.15, 1.0, 1.0), uv.y);
        col += glowC * edge * 0.9;
        col += bloom * 1.7;
        col = aces(col * 1.2);
    } else if (style == ST_BLUEPRINT) {
        // 청사진: 파란 종이 + 흰 선. 장면 밝기는 살짝만 남긴다
        float l = dot(col, vec3(0.299, 0.587, 0.114));
        vec2 g = abs(fract(uv / texel / 24.0) - 0.5);
        float grid = smoothstep(0.47, 0.5, max(g.x, g.y)) * 0.12;
        col = vec3(0.06, 0.2, 0.42) * (0.75 + 0.5 * clamp(l, 0.0, 1.0)) + vec3(0.85, 0.93, 1.0) * max(edge, grid);
        col += bloom * 0.3;
    } else {
        // 미니멀(데모 원래 화면)
        col = mix(col, vec3(0.05, 0.05, 0.07), edge * 0.85);
        col += bloom * 0.9;
        col = aces(col * 1.05);
    }

    float lum = dot(col, vec3(0.299, 0.587, 0.114));
    col = mix(col, vec3(lum) * vec3(0.82, 0.95, 1.12), focus * 0.6);

    float v = smoothstep(0.85, 0.25, length(uv - 0.5));
    float vig = style == ST_NOIR ? 0.6 : (style == ST_REAL ? 0.35 : 0.3);
    col *= mix(1.0, v, vig + focus * 0.45);
    col = mix(col, vec3(0.65, 0.02, 0.02), hurt * (1.0 - v) * 0.9);

    col += (hash(uv * 900.0 + time) - 0.5) * (style == ST_REAL ? 0.015 : 0.025);
    col = mix(col, fadeColor, clamp(fade, 0.0, 1.0));
    FragColor = vec4(pow(max(col, 0.0), vec3(1.0 / 2.2)), 1.0);
}
