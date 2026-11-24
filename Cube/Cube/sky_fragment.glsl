#version 330 core
in vec2 uv;
layout (location = 0) out vec4 outColor;
layout (location = 1) out vec4 outNormal;

uniform mat4 invViewProj;
uniform vec3 camPos;
uniform vec3 sunDir;
uniform vec3 zenith;
uniform vec3 horizon;
uniform vec3 sunColor;
uniform int space;      // 1 이면 우주 방: 별 + 성운 + 대기 없는 해

float hash3(vec3 p) { return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453); }
float noise3(vec3 p)
{
    vec3 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash3(i), hash3(i + vec3(1, 0, 0)), f.x), mix(hash3(i + vec3(0, 1, 0)), hash3(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(hash3(i + vec3(0, 0, 1)), hash3(i + vec3(1, 0, 1)), f.x), mix(hash3(i + vec3(0, 1, 1)), hash3(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}

// 방향을 격자로 나눠 칸마다 드물게 별 하나. 두 크기를 겹쳐 밝은 별과 잔별
float stars(vec3 dir, float scale, float density)
{
    vec3 q = dir * scale, i = floor(q), f = fract(q) - 0.5;
    float h = hash3(i);
    if (h < 1.0 - density) return 0.0;
    vec3 o = vec3(hash3(i + 7.1), hash3(i + 3.3), hash3(i + 5.9)) - 0.5;
    float d = length(f - o * 0.6);
    return smoothstep(0.12, 0.0, d) * (0.4 + 0.6 * (h - (1.0 - density)) / density);
}

vec3 spaceSky(vec3 dir)
{
    float n = noise3(dir * 2.2) * 0.55 + noise3(dir * 5.0) * 0.3 + noise3(dir * 11.0) * 0.15;
    float band = exp(-pow(dot(dir, normalize(vec3(0.3, 0.9, -0.3))) * 2.6, 2.0));   // 은하수 띠
    vec3 neb = mix(vec3(0.16, 0.04, 0.22), vec3(0.03, 0.18, 0.26), noise3(dir * 3.1 + 4.0));
    vec3 col = vec3(0.004, 0.005, 0.012) + neb * pow(n, 3.0) * (0.35 + 1.4 * band);
    float s = stars(dir, 90.0, 0.07) * 2.5 + stars(dir, 220.0, 0.12) * 1.2 + stars(dir, 400.0, 0.2) * band * 0.8;
    col += vec3(s) * mix(vec3(1.0, 0.85, 0.7), vec3(0.7, 0.85, 1.0), hash3(floor(dir * 90.0)));
    float sd = max(dot(dir, normalize(sunDir)), 0.0);
    col += vec3(1.6, 1.5, 1.35) * (pow(sd, 1500.0) * 60.0 + pow(sd, 60.0) * 0.25 + pow(sd, 8.0) * 0.03);
    return col;
}

void main()
{
    vec4 p = invViewProj * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec3 dir = normalize(p.xyz / p.w - camPos);
    if (space == 1) {
        outColor = vec4(spaceSky(dir), 1.0);
        outNormal = vec4(0.5, 0.5, 1.0, 1.0);
        return;
    }
    float h = dir.y;

    vec3 col = mix(horizon, zenith, smoothstep(-0.02, 0.6, h));
    if (h < 0.0) col = mix(horizon, horizon * 0.55, 1.0 - smoothstep(-0.3, 0.0, h));

    float s = max(dot(dir, normalize(sunDir)), 0.0);
    col += sunColor * (pow(s, 900.0) * 30.0 + pow(s, 12.0) * 0.35);

    outColor = vec4(col, 1.0);
    outNormal = vec4(0.5, 0.5, 1.0, 1.0);
}
