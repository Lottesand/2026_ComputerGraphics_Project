#version 330 core
in vec2 uv;
out vec4 FragColor;

uniform sampler2D sceneTex;
uniform sampler2D normalTex;
uniform sampler2D bloomTex;
uniform vec2 texel;
uniform float focus;     // 0~1 슬로모션 정도
uniform float hurt;      // 0~1 피격
uniform float time;

vec4 S(vec2 o) { return texture(normalTex, uv + o * texel); }

vec3 aces(vec3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

void main()
{
    vec2 dir = uv - 0.5;
    float ca = focus * 0.004 + hurt * 0.008;
    vec3 col;
    col.r = texture(sceneTex, uv + dir * ca).r;
    col.g = texture(sceneTex, uv).g;
    col.b = texture(sceneTex, uv - dir * ca).b;

    // 외곽선: 법선은 Sobel, 깊이는 라플라시안(평면에서는 0)
    vec4 c = S(vec2(0.0));
    vec4 gx = -S(vec2(-1, -1)) - 2.0 * S(vec2(-1, 0)) - S(vec2(-1, 1)) + S(vec2(1, -1)) + 2.0 * S(vec2(1, 0)) + S(vec2(1, 1));
    vec4 gy = -S(vec2(-1, -1)) - 2.0 * S(vec2(0, -1)) - S(vec2(1, -1)) + S(vec2(-1, 1)) + 2.0 * S(vec2(0, 1)) + S(vec2(1, 1));
    float lap = S(vec2(1, 0)).a + S(vec2(-1, 0)).a + S(vec2(0, 1)).a + S(vec2(0, -1)).a - 4.0 * c.a;
    float eN = length(vec2(length(gx.xyz), length(gy.xyz)));
    float eD = abs(lap) / max(c.a, 0.002);
    float edge = max(smoothstep(0.6, 1.2, eN), smoothstep(0.03, 0.08, eD));
    edge *= 1.0 - smoothstep(0.3, 0.7, c.a);
    col = mix(col, vec3(0.05, 0.05, 0.07), edge * 0.85);

    col += texture(bloomTex, uv).rgb * 0.9;
    col = aces(col * 1.05);

    float lum = dot(col, vec3(0.299, 0.587, 0.114));
    col = mix(col, vec3(lum) * vec3(0.82, 0.95, 1.12), focus * 0.6);

    float v = smoothstep(0.85, 0.25, length(dir));
    col *= mix(1.0, v, 0.3 + focus * 0.45);
    col = mix(col, vec3(0.65, 0.02, 0.02), hurt * (1.0 - v) * 0.9);

    col += (hash(uv * 900.0 + time) - 0.5) * 0.025;
    FragColor = vec4(pow(col, vec3(1.0 / 2.2)), 1.0);
}
