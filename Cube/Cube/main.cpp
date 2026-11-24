// Cube - 큐브 회전 던전 FPS (컴퓨터 그래픽스 최종 프로젝트, GT_Demo 프로토타입에서 시작)
//
// 렌더링 파이프라인
//  1) 그림자 패스 : 태양(방향광) 시점 직교투영으로 깊이 텍스처 생성 (Shadow Mapping)
//  2) 씬 패스     : HDR 프레임버퍼에 하늘 + Phong 조명 + PCF 그림자 + 반구 주변광, MRT로 법선/깊이 기록
//  3) 블룸        : 밝은 영역 추출 → 가우시안 블러 핑퐁 (절반 해상도)
//  4) 합성        : 외곽선(Sobel/라플라시안) + 블룸 + ACES 톤매핑 + 집중 모드 색보정/비네팅/색수차
//  5) HUD         : 2D 동차좌표 변환으로 픽셀 → NDC

#include <GL/glew.h>
#include <GL/glfw3.h>
#include <gl/glm/glm.hpp>
#include <gl/glm/ext.hpp>
#include <gl/glm/gtc/matrix_transform.hpp>
#include <gl/glm/gtc/type_ptr.hpp>
#include <gl/glm/gtc/quaternion.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <random>
#include <thread>
#include <numeric>
#include <cstdint>

const float PI = 3.1415926535f;
const float FAR_PLANE = 200.0f;
const int SHADOW_SIZE = 2048;
const float PLAYER_R = 0.35f;
const float PLAYER_H = 1.8f;
const float EYE_H = 1.62f;
const float STEP_H = 0.6f;
const float ENEMY_R = 0.38f;
const float ENEMY_H = 1.95f;

const glm::vec3 SUN_DIR = glm::normalize(glm::vec3(0.5f, 0.55f, 0.65f));   // 표면 → 태양
const glm::vec3 SUN_COLOR(1.55f, 1.25f, 0.95f);
const glm::vec3 SKY_ZENITH(0.14f, 0.2f, 0.42f);
const glm::vec3 SKY_HORIZON(1.0f, 0.66f, 0.45f);

// ===================================================================
// 공통 유틸
// ===================================================================
std::mt19937 rng(777);
float frand(float a, float b) { std::uniform_real_distribution<float> d(a, b); return d(rng); }

std::string filetobuf(const char* file) {
    std::ifstream shaderFile(file);
    if (!shaderFile.is_open()) {
        std::string altPath = std::string("Cube/") + file;
        shaderFile.open(altPath);
        if (!shaderFile.is_open()) return "";
    }
    return std::string((std::istreambuf_iterator<char>(shaderFile)), std::istreambuf_iterator<char>());
}

GLuint compileShader(GLenum type, const std::string& src, const char* name) {
    const char* s = src.c_str();
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &s, NULL);
    glCompileShader(sh);
    GLint ok;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(sh, 1024, NULL, log);
        std::cerr << "[셰이더 컴파일 오류] " << name << "\n" << log << std::endl;
    }
    return sh;
}

GLuint makeProgram(const char* vsFile, const char* fsFile) {
    std::string vs = filetobuf(vsFile), fs = filetobuf(fsFile);
    if (vs.empty() || fs.empty()) {
        std::cerr << "셰이더 파일을 찾을 수 없습니다: " << vsFile << ", " << fsFile << std::endl;
        return 0;
    }
    GLuint v = compileShader(GL_VERTEX_SHADER, vs, vsFile);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs, fsFile);
    GLuint program = glCreateProgram();
    glAttachShader(program, v);
    glAttachShader(program, f);
    glLinkProgram(program);
    GLint ok;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(program, 1024, NULL, log);
        std::cerr << "[셰이더 링크 오류] " << vsFile << " + " << fsFile << "\n" << log << std::endl;
        return 0;
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return program;
}

GLint U(GLuint p, const char* name) { return glGetUniformLocation(p, name); }

// ===================================================================
// 메쉬
// ===================================================================
struct Mesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLsizei count = 0;
};

Mesh cubeMesh, tetraMesh;

void pushVert(std::vector<float>& v, glm::vec3 p, glm::vec3 n) {
    v.insert(v.end(), { p.x, p.y, p.z, n.x, n.y, n.z, 1.0f, 1.0f, 1.0f });
}

void pushTri(std::vector<float>& v, glm::vec3 a, glm::vec3 b, glm::vec3 c) {
    glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
    pushVert(v, a, n); pushVert(v, b, n); pushVert(v, c, n);
}

void pushQuad(std::vector<float>& v, glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3) {
    pushTri(v, p0, p1, p2);
    pushTri(v, p0, p2, p3);
}

Mesh uploadMesh(const std::vector<float>& data) {
    Mesh m;
    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
    m.count = (GLsizei)(data.size() / 9);
    return m;
}

Mesh buildCube() {
    float s = 0.5f;
    glm::vec3 v000(-s, -s, -s), v100(s, -s, -s), v110(s, s, -s), v010(-s, s, -s);
    glm::vec3 v001(-s, -s, s), v101(s, -s, s), v111(s, s, s), v011(-s, s, s);
    std::vector<float> d;
    pushQuad(d, v001, v101, v111, v011);
    pushQuad(d, v101, v100, v110, v111);
    pushQuad(d, v100, v000, v010, v110);
    pushQuad(d, v000, v001, v011, v010);
    pushQuad(d, v011, v111, v110, v010);
    pushQuad(d, v000, v100, v101, v001);
    return uploadMesh(d);
}

// 파편용 사면체
Mesh buildTetra() {
    glm::vec3 a(0.5f, 0.5f, 0.5f), b(-0.5f, -0.5f, 0.5f), c(-0.5f, 0.5f, -0.5f), e(0.5f, -0.5f, -0.5f);
    std::vector<float> d;
    pushTri(d, a, b, e);
    pushTri(d, a, c, b);
    pushTri(d, a, e, c);
    pushTri(d, b, c, e);
    return uploadMesh(d);
}

// ===================================================================
// 텍스처: 이미지 파일 없이 코드로 그린 512x512 이미지 6장을 텍스처 배열 한 개에 올린다
//   RGB = 색(재질 색에 곱함), A = 반사(스펙큘러) 세기
// ===================================================================
enum TexLayer { TEX_CONCRETE = 0, TEX_TILE, TEX_GLASS, TEX_METAL, TEX_PANEL, TEX_CRATE, TEX_COUNT };
const int TEX_SIZE = 512;
GLuint albedoArray = 0;

float thash(int x, int y, int seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xFFFFFF) / 16777215.0f;
}

// 타일링되는 값 노이즈 (period 칸마다 반복 → 텍스처 경계가 이어진다)
float tnoise(float x, float y, int period, int seed) {
    int x0 = (int)floorf(x), y0 = (int)floorf(y);
    float fx = x - x0, fy = y - y0;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    auto h = [&](int a, int b) { return thash(((a % period) + period) % period, ((b % period) + period) % period, seed); };
    float a = h(x0, y0) + (h(x0 + 1, y0) - h(x0, y0)) * fx;
    float b = h(x0, y0 + 1) + (h(x0 + 1, y0 + 1) - h(x0, y0 + 1)) * fx;
    return a + (b - a) * fy;
}

float tfbm(float u, float v, int baseFreq, int seed) {
    float s = 0.0f, amp = 0.5f;
    for (int o = 0; o < 5; ++o) {
        int f = baseFreq << o;
        s += amp * tnoise(u * f, v * f, f, seed + o * 17);
        amp *= 0.5f;
    }
    return s;
}

// 격자선까지의 거리 (0~0.5). 홈·줄눈을 그릴 때 쓴다
float gridDist(float u, int cells) { float f = u * cells; return fabsf(f - floorf(f) - 0.5f); }

glm::vec4 texel(int layer, float u, float v) {
    float n = tfbm(u, v, 8, layer * 101);
    float fine = tfbm(u, v, 64, layer * 131 + 7);
    switch (layer) {
    case TEX_CONCRETE: {
        // 거푸집 판 자국 + 폼타이 구멍 + 세로 얼룩
        float seam = std::min(gridDist(u, 2), gridDist(v, 4));
        float g = 0.78f + 0.22f * n + 0.08f * fine;
        if (seam > 0.485f) g *= 0.72f;
        float hx = u * 4.0f - floorf(u * 4.0f) - 0.5f, hy = v * 8.0f - floorf(v * 8.0f) - 0.5f;
        if (hx * hx + hy * hy < 0.004f) g *= 0.45f;
        g *= 1.0f - 0.12f * tnoise(u * 32.0f, v * 2.0f, 32, 99) * tnoise(u * 32.0f, v * 2.0f, 32, 98);
        return glm::vec4(g, g, g * 0.98f, 0.15f);
    }
    case TEX_TILE: {
        // 바닥 타일 4x4, 줄눈, 타일마다 다른 밝기와 닳은 자국
        float d = std::max(gridDist(u, 4), gridDist(v, 4));
        int tx = (int)(u * 4), ty = (int)(v * 4);
        float t = 0.86f + 0.12f * thash(tx, ty, 5) + 0.06f * fine - 0.1f * std::max(0.0f, n - 0.55f);
        if (d > 0.47f) return glm::vec4(0.32f, 0.32f, 0.33f, 0.05f);
        return glm::vec4(t, t, t * 1.01f, 0.75f);
    }
    case TEX_GLASS: {
        // 판유리 2x2 + 창틀. 위로 갈수록 밝은 반사, 유리마다 다른 색조, 먼지
        float d = std::max(gridDist(u, 2), gridDist(v, 2));
        if (d > 0.465f) return glm::vec4(0.2f, 0.21f, 0.24f, 0.4f);
        int px = (int)(u * 2), py = (int)(v * 2);
        float fv = v * 2 - py;
        float refl = 0.7f + 0.35f * fv + 0.15f * thash(px, py, 3) + 0.08f * n;
        float streak = 0.08f * tnoise(u * 40.0f, v * 3.0f, 40, 55);
        return glm::vec4(refl * 0.92f - streak, refl * 0.97f - streak, refl * 1.05f - streak, 1.0f);
    }
    case TEX_METAL: {
        // 열차 외판: 가로 헤어라인 + 세로 패널 이음매 + 리벳
        float brushed = 0.85f + 0.1f * tnoise(u * 4.0f, v * 160.0f, 4, 31) + 0.05f * n;
        float seam = gridDist(u, 4);
        if (seam > 0.49f) brushed *= 0.55f;
        float rx = u * 16.0f - floorf(u * 16.0f) - 0.5f, ry = v * 2.0f - floorf(v * 2.0f) - 0.5f;
        if (fabsf(ry) > 0.44f && rx * rx < 0.01f) brushed *= 1.15f;
        return glm::vec4(brushed, brushed * 1.01f, brushed * 1.04f, 0.95f);
    }
    case TEX_PANEL: {
        // 방 벽: 크고 작은 금속 패널, 테두리 홈, 모서리 볼트
        float big = std::max(gridDist(u, 2), gridDist(v, 2));
        float small = std::max(gridDist(u, 4), gridDist(v, 8));
        float g = 0.82f + 0.12f * n + 0.05f * fine;
        if (big > 0.485f) g *= 0.5f;
        else if (small > 0.49f) g *= 0.8f;
        float bx = u * 2 - floorf(u * 2), by = v * 2 - floorf(v * 2);
        for (float cx : { 0.06f, 0.94f })
            for (float cy : { 0.06f, 0.94f })
                if ((bx - cx) * (bx - cx) + (by - cy) * (by - cy) < 0.0006f) g *= 0.55f;
        return glm::vec4(g, g, g, 0.45f);
    }
    default: {
        // 상자: 테두리 틀 + 가로 판자
        float edge = std::max(fabsf(u - 0.5f), fabsf(v - 0.5f));
        float plank = gridDist(v, 5);
        float g = 0.8f + 0.15f * tnoise(u * 6.0f, v * 60.0f, 6, 71) + 0.08f * n;
        if (edge > 0.42f) g *= 0.75f;
        else if (plank > 0.48f) g *= 0.6f;
        return glm::vec4(g, g * 0.97f, g * 0.94f, 0.2f);
    }
    }
}

void createTextures() {
    std::vector<uint8_t> px((size_t)TEX_SIZE * TEX_SIZE * 4 * TEX_COUNT);
    // 층마다 스레드 하나씩 (이미지 생성은 CPU 작업이라 나눠서 빠르게)
    std::vector<std::thread> workers;
    for (int layer = 0; layer < TEX_COUNT; ++layer)
        workers.emplace_back([&px, layer]() {
            for (int y = 0; y < TEX_SIZE; ++y)
                for (int x = 0; x < TEX_SIZE; ++x) {
                    glm::vec4 c = glm::clamp(texel(layer, (x + 0.5f) / TEX_SIZE, (y + 0.5f) / TEX_SIZE), 0.0f, 1.0f);
                    uint8_t* p = &px[(((size_t)layer * TEX_SIZE + y) * TEX_SIZE + x) * 4];
                    for (int k = 0; k < 4; ++k) p[k] = (uint8_t)(c[k] * 255.0f + 0.5f);
                }
        });
    for (auto& w : workers) w.join();

    glGenTextures(1, &albedoArray);
    glBindTexture(GL_TEXTURE_2D_ARRAY, albedoArray);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, TEX_SIZE, TEX_SIZE, TEX_COUNT, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);   // 멀리 있는 면의 반짝임(모아레) 방지
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    if (GLEW_EXT_texture_filter_anisotropic) {
        float maxAniso = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
        glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(8.0f, maxAniso));
    }
}

// ===================================================================
// 월드 데이터
// ===================================================================
struct Material {
    glm::vec3 color;
    float ks, shininess, emissive;
    int pattern;
    int tex = -1;           // 텍스처 배열의 층 (-1 이면 텍스처 없음)
    float texScale = 4.0f;  // 텍스처 한 장이 덮는 월드 길이
};

struct Box {
    glm::vec3 center, size;
    Material mat;
    bool solid;
    glm::vec3 shellDir = glm::vec3(0.0f);   // 방 외벽이면 바깥 방향, 아니면 0
    bool glassy = false;                    // 반투명 유리(난간 등): 불투명한 것을 다 그린 뒤 뒤에서부터 그린다
    glm::vec3 bmin() const { return center - size * 0.5f; }
    glm::vec3 bmax() const { return center + size * 0.5f; }
};

struct Enemy {
    glm::vec3 pos;
    float yaw, shootCd, walkT, strafe, spawnT, aimT, vy;
    bool alive;
};

struct Bullet {
    glm::vec3 pos, vel;
    bool fromPlayer, alive;
    float life;
};

struct Shard {
    glm::vec3 pos, vel, axis, color;
    float angle, angVel, size, life;
    bool glow;
};

struct Spark {
    glm::vec3 pos, vel, color;
    float life;
};

struct Player {
    glm::vec3 pos, vel;
    float yaw, pitch;
    bool onGround, dead;
    int hp;
    float fireCd, recoil, bobPhase, hurtT, landDip, deadT, muzzleT;
} player;

const Material M_FLOOR{ glm::vec3(0.8f, 0.78f, 0.74f), 0.25f, 24.0f, 0.0f, 1, TEX_TILE, 6.0f };
const Material M_CONCRETE{ glm::vec3(0.88f, 0.86f, 0.82f), 0.15f, 16.0f, 0.0f, 2, TEX_CONCRETE, 4.0f };
const Material M_DARK{ glm::vec3(0.16f, 0.17f, 0.2f), 0.35f, 32.0f, 0.0f, 0 };
const Material M_CRATE{ glm::vec3(0.78f, 0.42f, 0.28f), 0.2f, 16.0f, 0.0f, 2, TEX_CRATE, -1.0f };
const Material M_NEON{ glm::vec3(0.25f, 0.9f, 1.0f), 0.0f, 1.0f, 3.5f, 0 };
const Material M_ENEMY{ glm::vec3(0.06f, 0.5f, 0.55f), 1.1f, 90.0f, 0.0f, 0 };
const Material M_ENEMY_DARK{ glm::vec3(0.03f, 0.2f, 0.24f), 0.9f, 64.0f, 0.0f, 0 };
const Material M_VISOR{ glm::vec3(0.4f, 1.0f, 0.95f), 0.0f, 1.0f, 5.0f, 0 };

std::vector<Box> localBoxes;    // 회전 전 방의 원본
std::vector<Box> boxes;         // 현재 방향으로 회전된 실제 월드

// 점광원 (방 좌표계). 그릴 때 방 회전을 적용하고 카메라에서 가까운 것만 셰이더로 보낸다
struct LightSrc { glm::vec3 pos, col; float range; };
std::vector<LightSrc> localLights;
std::vector<Enemy> enemies;
std::vector<Bullet> bullets;
std::vector<Shard> shards;
std::vector<Spark> sparks;

int wave = 0, kills = 0;
float waveTimer = 0.0f, waveBannerT = 0.0f, helpT = 9.0f;
float timeScale = 1.0f, focusEnergy = 1.0f, globalTime = 0.0f;
bool focusActive = false;
float fps = 0.0f;

GLFWwindow* gWindow = nullptr;

void addBox(glm::vec3 c, glm::vec3 s, const Material& m, bool solid = true) {
    localBoxes.push_back({ c, s, m, solid });
}

void buildArena() {
    // 난간
    addBox({ 0, 0.6f, -22.5f }, { 46, 1.2f, 1 }, M_CONCRETE);
    addBox({ 0, 0.6f, 22.5f }, { 46, 1.2f, 1 }, M_CONCRETE);
    addBox({ -22.5f, 0.6f, 0 }, { 1, 1.2f, 46 }, M_CONCRETE);
    addBox({ 22.5f, 0.6f, 0 }, { 1, 1.2f, 46 }, M_CONCRETE);

    // 높은 벽 + 네온 띠
    addBox({ -12, 2.5f, -6 }, { 0.8f, 5, 10 }, M_CONCRETE);
    addBox({ -11.58f, 4.3f, -6 }, { 0.04f, 0.1f, 9.6f }, M_NEON, false);
    addBox({ 12, 2.5f, 8 }, { 0.8f, 5, 8 }, M_CONCRETE);
    addBox({ 11.58f, 4.3f, 8 }, { 0.04f, 0.1f, 7.6f }, M_NEON, false);
    addBox({ -4, 2.5f, -15 }, { 10, 5, 0.8f }, M_CONCRETE);
    addBox({ -4, 4.3f, -14.58f }, { 9.6f, 0.1f, 0.04f }, M_NEON, false);

    // 퍼걸러: 기둥 + 들보 + 살 → 줄무늬 그림자
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2)
            addBox({ sx * 10.0f, 3.5f, sz * 10.0f }, { 1, 7, 1 }, M_CONCRETE);
    addBox({ 0, 7.2f, -10 }, { 21, 0.4f, 0.6f }, M_DARK);
    addBox({ 0, 7.2f, 10 }, { 21, 0.4f, 0.6f }, M_DARK);
    for (float x = -9.6f; x <= 9.7f; x += 1.2f)
        addBox({ x, 7.55f, 0 }, { 0.3f, 0.3f, 20.6f }, M_DARK);

    // 상자
    addBox({ -5, 0.6f, 3 }, glm::vec3(1.2f), M_CRATE);
    addBox({ -3.75f, 0.6f, 3.2f }, glm::vec3(1.2f), M_CRATE);
    addBox({ -4.4f, 1.8f, 3.1f }, glm::vec3(1.2f), M_CRATE);
    addBox({ 6, 0.75f, -4 }, glm::vec3(1.5f), M_CRATE);
    addBox({ 7.3f, 0.5f, -5 }, glm::vec3(1.0f), M_CRATE);
    addBox({ 3, 0.6f, 10 }, glm::vec3(1.2f), M_CRATE);
    addBox({ -8, 0.6f, 12 }, glm::vec3(1.2f), M_CRATE);
    addBox({ 16, 0.75f, 14 }, glm::vec3(1.5f), M_CRATE);

    // 엄폐 벽
    addBox({ -6, 0.6f, 7 }, { 4, 1.2f, 0.4f }, M_CONCRETE);
    addBox({ 5, 0.6f, 4 }, { 0.4f, 1.2f, 4 }, M_CONCRETE);
    addBox({ 0, 0.6f, -6 }, { 5, 1.2f, 0.4f }, M_CONCRETE);
    addBox({ -15, 0.6f, 6 }, { 0.4f, 1.2f, 5 }, M_CONCRETE);

    // 계단 + 단상
    addBox({ 15, 1.25f, -12 }, { 8, 2.5f, 8 }, M_CONCRETE);
    for (int i = 0; i < 5; ++i) {
        float h = (i + 1) * 0.5f;
        addBox({ 7.4f + 0.8f * i, h * 0.5f, -12 }, { 0.8f, h, 3 }, M_CONCRETE);
    }
    addBox({ 15, 2.52f, -7.98f }, { 8, 0.06f, 0.06f }, M_NEON, false);
    addBox({ 15, 3.1f, -15.5f }, { 6, 1.2f, 0.4f }, M_CONCRETE);

    // 네온 띠마다 점광원
    for (glm::vec3 p : { glm::vec3(-11.0f, 4.3f, -6), glm::vec3(11.0f, 4.3f, 8), glm::vec3(-4, 4.3f, -14.0f), glm::vec3(15, 2.6f, -7.4f) })
        localLights.push_back({ p, M_NEON.color * 2.5f, 13.0f });
}

// ===================================================================
// 큐브 월드: 27칸이 각각 정육면체 방 하나
// ===================================================================
const float ROOM_SCALE = 1.3f;                      // 데모(한 변 46) 대비 방 크기 배율
const float ROOM_HALF = 23.0f * ROOM_SCALE;
const glm::vec3 ROOM_C(0.0f, ROOM_HALF, 0.0f);      // 방은 이 점을 중심으로 회전 (바닥은 항상 y = 0)
const glm::vec3 FACE_DIRS[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
const glm::vec3 FACE_COLORS[6] = {
    { 0.85f, 0.14f, 0.14f }, { 1.0f, 0.48f, 0.08f }, { 0.95f, 0.95f, 0.95f },
    { 1.0f, 0.85f, 0.12f }, { 0.12f, 0.68f, 0.25f }, { 0.13f, 0.33f, 0.85f },
};
const int HUB_CELL = 13;        // (0, 0, 0) 정중앙 칸. 어느 면을 돌려도 위치가 변하지 않는 기차역 허브
const int ARENA_CELL = 26;      // (1, 1, 1) 꼭짓점 칸. 데모 때 만든 경기장 샘플이 이 칸의 바닥에 놓인다
const int ROOM_WAVES = 2;       // 이 수만큼 웨이브를 막으면 방 클리어 → 포털이 열린다
bool roomCleared[27];           // 칸(방) 번호 기준. 방이 회전해 자리를 옮겨도 클리어 기록은 방을 따라간다

struct Theme { glm::vec3 wall, crate, neon; };
const Theme THEMES[6] = {
    { { 0.88f, 0.86f, 0.82f }, { 0.78f, 0.42f, 0.28f }, { 0.25f, 0.9f, 1.0f } },
    { { 0.55f, 0.65f, 0.5f }, { 0.35f, 0.45f, 0.25f }, { 0.5f, 1.0f, 0.4f } },
    { { 0.85f, 0.72f, 0.52f }, { 0.6f, 0.4f, 0.22f }, { 1.0f, 0.7f, 0.25f } },
    { { 0.7f, 0.8f, 0.92f }, { 0.45f, 0.58f, 0.75f }, { 0.6f, 0.85f, 1.0f } },
    { { 0.5f, 0.32f, 0.3f }, { 0.28f, 0.16f, 0.15f }, { 1.0f, 0.35f, 0.2f } },
    { { 0.62f, 0.55f, 0.78f }, { 0.4f, 0.3f, 0.6f }, { 0.85f, 0.5f, 1.0f } },
};

struct Cubie {
    glm::ivec3 pos;     // -1 ~ 1
    glm::mat3 orient;   // 방 → 큐브 공간 회전. 90도 단위라 원소는 항상 0, 1, -1
};

Cubie cubies[27];
int currentCell = HUB_CELL;
glm::mat3 worldOrient(1.0f);    // 지금 월드에 실제로 적용된 방향

// 축(0 x, 1 y, 2 z) 기준 오른손 방향 90도 * turns 회전. 정수 행렬이라 오차가 쌓이지 않는다
glm::mat3 quarterTurn(int axis, int turns) {
    glm::mat3 q(1.0f);
    if (axis == 0) q = glm::mat3(glm::vec3(1, 0, 0), glm::vec3(0, 0, 1), glm::vec3(0, -1, 0));
    else if (axis == 1) q = glm::mat3(glm::vec3(0, 0, -1), glm::vec3(0, 1, 0), glm::vec3(1, 0, 0));
    else q = glm::mat3(glm::vec3(0, 1, 0), glm::vec3(-1, 0, 0), glm::vec3(0, 0, 1));
    glm::mat3 r(1.0f);
    for (int i = 0; i < ((turns % 4) + 4) % 4; ++i) r = q * r;
    return r;
}

void initCube() {
    for (int z = -1; z <= 1; ++z)
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x)
                cubies[(x + 1) + (y + 1) * 3 + (z + 1) * 9] = { glm::ivec3(x, y, z), glm::mat3(1.0f) };
}

int cubieAt(glm::ivec3 p) {
    for (int i = 0; i < 27; ++i)
        if (cubies[i].pos == p) return i;
    return -1;
}

void turnSlice(int axis, int layer, int turns) {
    glm::mat3 r = quarterTurn(axis, turns);
    for (auto& c : cubies)
        if (c.pos[axis] == layer) {
            c.pos = glm::ivec3(glm::round(r * glm::vec3(c.pos)));
            c.orient = r * c.orient;
        }
}

// 방의 한 면(face)에서 안쪽으로 솟는 구조물을 무작위로 배치
void addFaceProps(int face, std::mt19937& r, const Theme& t) {
    auto rnd = [&](float a, float b) { std::uniform_real_distribution<float> d(a, b); return d(r); };
    int axis = face / 2;
    glm::vec3 u(0.0f), v(0.0f), in = -FACE_DIRS[face];
    u[(axis + 1) % 3] = 1.0f;
    v[(axis + 2) % 3] = 1.0f;
    Material wall{ t.wall, 0.15f, 16.0f, 0.0f, 2, TEX_CONCRETE, 4.0f }, crate{ t.crate, 0.2f, 16.0f, 0.0f, 2, TEX_CRATE, -1.0f };
    Material neon{ t.neon, 0.0f, 1.0f, 3.5f, 0 }, fixture{ glm::vec3(1.0f, 0.93f, 0.8f), 0.0f, 1.0f, 2.5f, 0 };
    auto place = [&](float a, float b, float su, float sv, float h0, float h, const Material& m, bool solid = true) {
        glm::vec3 c = ROOM_C + FACE_DIRS[face] * ROOM_HALF + u * a + v * b + in * (h0 + h * 0.5f);
        addBox(c, u * su + v * sv + glm::abs(in) * h, m, solid);
        return c;
    };
    // 면마다 벽등 4개: 빛나는 판 + 점광원
    for (int sa = -1; sa <= 1; sa += 2)
        for (int sb = -1; sb <= 1; sb += 2) {
            glm::vec3 c = place(sa * 13.0f * ROOM_SCALE, sb * 13.0f * ROOM_SCALE, 1.6f, 0.5f, 0.0f, 0.12f, fixture, false);
            localLights.push_back({ c + in * 1.2f, glm::vec3(1.0f, 0.88f, 0.7f) * 2.2f, 15.0f });
        }
    // 방이 커진 만큼 배치 범위는 배율, 개수는 면적(배율의 제곱)에 맞춘다
    const float k = ROOM_SCALE, k2 = ROOM_SCALE * ROOM_SCALE;
    for (int i = 0; i < (int)(9 * k2); ++i) {
        float s = rnd(1.0f, 2.0f), a = rnd(-19 * k, 19 * k), b = rnd(-19 * k, 19 * k);
        place(a, b, s, s, 0.0f, s, crate);
        if (i % 3 == 0) place(a + 0.1f, b - 0.1f, s * 0.8f, s * 0.8f, s, s * 0.8f, crate);
    }
    for (int i = 0; i < (int)(5 * k2); ++i) {
        bool alongU = i % 2 == 0;
        float len = rnd(3, 7);
        place(rnd(-17 * k, 17 * k), rnd(-17 * k, 17 * k), alongU ? len : 0.4f, alongU ? 0.4f : len, 0.0f, 1.2f, wall);
    }
    for (int i = 0; i < (int)(4 * k2); ++i) {
        float a = rnd(-16 * k, 16 * k), b = rnd(-16 * k, 16 * k), h = rnd(4, 9);
        place(a, b, 1.0f, 1.0f, 0.0f, h, wall);
        glm::vec3 c = place(a, b, 1.04f, 1.04f, h - 0.5f, 0.1f, neon, false);
        localLights.push_back({ c, t.neon * 2.5f, 12.0f });
    }
    // 단상 + 계단
    float pa = rnd(4 * k, 12 * k), pb = rnd(-12 * k, 12 * k);
    place(pa, pb, 8.0f, 8.0f, 0.0f, 2.5f, wall);
    for (int i = 0; i < 5; ++i)
        place(pa - 4.0f - 3.6f + 0.8f * i, pb, 0.8f, 3.0f, 0.0f, (i + 1) * 0.5f, wall);
}

// ===================================================================
// 허브: 큐브 정중앙의 기차역. 8방향 선로를 따라 기차가 계속 달린다
// ===================================================================
glm::mat4 TRS(glm::vec3 t, glm::vec3 s);

// 방 좌표계 → 월드: 방 중심을 기준으로 현재 방향(worldOrient)만큼 회전
glm::mat4 roomToWorld() {
    return glm::translate(glm::mat4(1.0f), ROOM_C) * glm::mat4(worldOrient) * glm::translate(glm::mat4(1.0f), -ROOM_C);
}

// 회전된 상자(대각선 선로 등). 축 정렬 상자가 아니라 충돌은 없고 그리기만 한다 (방 좌표계)
struct Decor { glm::mat4 model; Material mat; };
std::vector<Decor> localDecor;

// 선로: 방 좌표계의 직선. 기차 머리는 sMin(벽 속)에서 나와 sMax(반대쪽 벽 속)로 들어간다
struct Track {
    glm::vec3 c, dir;       // 선로 위의 한 점(높이 포함), 진행 방향(수평 단위 벡터)
    float sMin, sMax, speed, phase;
    int cars;
};
std::vector<Track> tracks;
const float CAR_LEN = 7.0f, CAR_GAP = 0.6f, CAR_W = 2.6f, CAR_H = 3.0f;
float trainTime = 0.0f;

// 선로 위 거리 s 지점의 좌표계: x = 진행 방향, y = 위, z = 옆 (방 좌표계)
glm::mat4 trackFrame(const Track& t, float s, float yOff) {
    glm::vec3 p = t.c + t.dir * s + glm::vec3(0, yOff, 0);
    glm::vec3 z = glm::normalize(glm::cross(t.dir, glm::vec3(0, 1, 0)));
    return glm::mat4(glm::vec4(t.dir, 0), glm::vec4(0, 1, 0, 0), glm::vec4(z, 0), glm::vec4(p, 1));
}

// i번째 객차 중심의 선로 위 거리. 한 바퀴(벽→벽 + 열차 길이 + 쉬는 구간)마다 반복
float carS(const Track& t, int i) {
    float trainLen = t.cars * (CAR_LEN + CAR_GAP);
    float period = (t.sMax - t.sMin) + trainLen + 30.0f;
    float head = t.sMin + fmodf(t.phase + t.speed * trainTime, period);
    return head - CAR_LEN * 0.5f - i * (CAR_LEN + CAR_GAP);
}

bool carVisible(const Track& t, float s) { return s > t.sMin - CAR_LEN && s < t.sMax + CAR_LEN; }

void buildHub() {
    const float H = ROOM_HALF;
    Material glass{ glm::vec3(0.4f, 0.47f, 0.56f), 0.8f, 96.0f, 0.05f, 3, TEX_GLASS, 12.8f };
    Material floorM{ glm::vec3(0.42f, 0.43f, 0.46f), 0.5f, 48.0f, 0.0f, 1, TEX_TILE, 8.0f };
    Material conc{ glm::vec3(0.52f, 0.53f, 0.56f), 0.15f, 16.0f, 0.0f, 2, TEX_CONCRETE, 8.0f };
    Material railM{ glm::vec3(0.2f, 0.21f, 0.23f), 0.8f, 64.0f, 0.0f, 0 };
    Material lamp{ glm::vec3(0.85f, 0.92f, 1.0f), 0.0f, 1.0f, 2.2f, 0 };
    Material railGlass{ glm::vec3(0.55f, 0.7f, 0.8f), 1.0f, 128.0f, 0.15f, 0 };
    Material board{ glm::vec3(0.06f, 0.07f, 0.08f), 0.4f, 32.0f, 0.0f, 0 };
    Material amber{ glm::vec3(1.0f, 0.6f, 0.15f), 0.0f, 1.0f, 2.8f, 0 };
    const glm::vec3 coolLight = glm::vec3(0.8f, 0.9f, 1.0f) * 2.4f;
    for (int f = 0; f < 6; ++f) {
        glm::vec3 d = FACE_DIRS[f], ad = glm::abs(d);
        Box shell{ ROOM_C + d * (H + 0.5f), ad + (glm::vec3(1.0f) - ad) * (H * 2.0f + 2.0f), f == 3 ? floorM : glass, true };
        shell.shellDir = d;
        localBoxes.push_back(shell);
    }
    // 세 층의 둘레 발코니(콘코스)와 가장자리 조명
    const float bw = 4.5f;
    for (int lv = 1; lv <= 3; ++lv) {
        float y = lv * 13.0f;
        for (int s = -1; s <= 1; s += 2) {
            addBox({ 0, y - 0.4f, s * (H - bw * 0.5f) }, { H * 2.0f, 0.8f, bw }, conc);
            addBox({ s * (H - bw * 0.5f), y - 0.4f, 0 }, { bw, 0.8f, H * 2.0f - bw * 2.0f }, conc);
            addBox({ 0, y - 0.85f, s * (H - bw) }, { H * 2.0f - bw * 2.0f, 0.1f, 0.25f }, lamp, false);
            addBox({ s * (H - bw), y - 0.85f, 0 }, { 0.25f, 0.1f, H * 2.0f - bw * 2.0f }, lamp, false);
        }
    }
    // 모서리 기둥과 층을 가로지르는 다리
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2)
            addBox({ sx * (H - 1.6f), H, sz * (H - 1.6f) }, { 3.2f, H * 2.0f, 3.2f }, conc);
    addBox({ 10.0f, 12.6f, 0 }, { 4.0f, 0.8f, H * 2.0f - bw * 2.0f }, conc);
    addBox({ 0, 25.6f, -9.0f }, { H * 2.0f - bw * 2.0f, 0.8f, 4.0f }, conc);
    addBox({ -7.0f, 38.6f, 0 }, { 4.0f, 0.8f, H * 2.0f - bw * 2.0f }, conc);
    // 바닥 콘코스: 벤치와 조명 기둥
    for (int i = 0; i < 6; ++i) {
        float a = i * PI / 3.0f;
        addBox({ cosf(a) * 9.0f, 0.25f, sinf(a) * 9.0f }, { 2.4f, 0.5f, 0.8f }, conc);
        addBox({ cosf(a) * 15.0f, 1.6f, sinf(a) * 15.0f }, { 0.3f, 3.2f, 0.3f }, railM);
        addBox({ cosf(a) * 15.0f, 3.3f, sinf(a) * 15.0f }, { 1.6f, 0.5f, 0.12f }, lamp, false);
        localLights.push_back({ glm::vec3(cosf(a) * 15.0f, 3.6f, sinf(a) * 15.0f), coolLight, 14.0f });
    }

    // 계단 두 개: 바닥 → 1층(z+ 벽 쪽, x 방향), 1층 → 2층(x- 벽 쪽, z 방향). 단 높이 0.5 라 걸어서 오를 수 있다
    // 선로가 지나가지 않는 자리에 둔다
    const float sz = H - bw - 1.5f;
    for (int i = 0; i < 26; ++i) {
        addBox({ -10.0f + 0.8f * i, 0.25f + 0.5f * i, sz }, { 0.8f, 0.5f, 3.0f }, conc);
        addBox({ -sz, 13.25f + 0.5f * i, 10.0f - 0.8f * i }, { 3.0f, 0.5f, 0.8f }, conc);
    }
    float slope = atanf(0.5f / 0.8f);
    localDecor.push_back({ glm::translate(glm::mat4(1.0f), glm::vec3(0, 6.6f, sz - 1.6f)) * glm::rotate(glm::mat4(1.0f), slope, glm::vec3(0, 0, 1)) *
                           glm::scale(glm::mat4(1.0f), glm::vec3(24.5f, 0.12f, 0.12f)), lamp });
    localDecor.push_back({ glm::translate(glm::mat4(1.0f), glm::vec3(-sz + 1.6f, 19.6f, 0)) * glm::rotate(glm::mat4(1.0f), slope, glm::vec3(1, 0, 0)) *
                           glm::scale(glm::mat4(1.0f), glm::vec3(0.12f, 0.12f, 24.5f)), lamp });

    // 발코니 안쪽 가장자리의 유리 난간(반투명). 계단 끝과 다리가 닿는 곳은 비워 둔다
    // {층, 난간이 놓인 축(0: x=±, 2: z=±), 부호, 비울 구간}
    struct Gap { int lv, axis, sign; float a, b; };
    const Gap gaps[] = {
        { 1, 2, 1, 5.5f, 12.5f }, { 1, 2, -1, 7.5f, 12.5f }, { 1, 0, -1, 7.5f, 12.5f },
        { 2, 0, 1, -11.5f, -6.5f }, { 2, 0, -1, -12.5f, -5.5f },
        { 3, 2, 1, -9.5f, -4.5f }, { 3, 2, -1, -9.5f, -4.5f },
    };
    for (int lv = 1; lv <= 3; ++lv) {
        float y = lv * 13.0f + 0.5f;
        for (int axis : { 0, 2 })
            for (int sign = -1; sign <= 1; sign += 2) {
                // 이 변의 난간을 구간으로 나눠서 틈을 뺀 부분만 세운다
                std::vector<std::pair<float, float>> segs{ { -(H - bw), H - bw } };
                for (const Gap& g : gaps) {
                    if (g.lv != lv || g.axis != axis || g.sign != sign) continue;
                    std::vector<std::pair<float, float>> next;
                    for (auto [a, b] : segs) {
                        if (g.b <= a || g.a >= b) { next.push_back({ a, b }); continue; }
                        if (g.a > a) next.push_back({ a, g.a });
                        if (g.b < b) next.push_back({ g.b, b });
                    }
                    segs = next;
                }
                for (auto [a, b] : segs) {
                    float mid = (a + b) * 0.5f, len = b - a;
                    glm::vec3 c(0, y, 0), s(0.12f, 1.0f, 0.12f);
                    int along = axis == 2 ? 0 : 2;      // 변이 z = ± 면 x 방향으로 뻗는다
                    c[axis] = sign * (H - bw + 0.1f);
                    c[along] = mid;
                    s[along] = len;
                    Box rail{ c, s, railGlass, true };
                    rail.glassy = true;
                    localBoxes.push_back(rail);
                    glm::vec3 top = c + glm::vec3(0, 0.52f, 0);
                    glm::vec3 ts = s;
                    ts.y = 0.06f;
                    ts[axis] = 0.16f;
                    addBox(top, ts, railM);
                }
            }
        // 발코니 아래 조명: 변마다 3개
        for (int axis : { 0, 2 })
            for (int sign = -1; sign <= 1; sign += 2)
                for (int j = -1; j <= 1; ++j) {
                    glm::vec3 p(0, lv * 13.0f - 1.6f, 0);
                    p[axis] = sign * (H - bw - 0.5f);
                    p[axis == 2 ? 0 : 2] = j * 15.0f;
                    localLights.push_back({ p, coolLight * 0.9f, 15.0f });
                }
    }

    // 천장에 매달린 출발 안내판 4개 (호박색 글자판) + 지붕 조명
    for (int i = 0; i < 4; ++i) {
        float a = i * PI * 0.5f + PI * 0.25f;
        glm::vec3 p(cosf(a) * 11.0f, 50.0f, sinf(a) * 11.0f);
        bool alongX = i % 2 == 0;
        glm::vec3 size = alongX ? glm::vec3(6.0f, 1.4f, 0.35f) : glm::vec3(0.35f, 1.4f, 6.0f);
        addBox(p, size, board, false);
        glm::vec3 face = size + (alongX ? glm::vec3(-0.4f, -0.5f, 0.04f) : glm::vec3(0.04f, -0.5f, -0.4f));
        addBox(p + glm::vec3(0, 0.05f, 0), face, amber, false);
        for (int s = -1; s <= 1; s += 2) {
            glm::vec3 rod = p + (alongX ? glm::vec3(s * 2.5f, 0, 0) : glm::vec3(0, 0, s * 2.5f)) + glm::vec3(0, 5.0f, 0);
            addBox(rod, { 0.08f, 9.3f, 0.08f }, railM, false);
        }
        localLights.push_back({ p - glm::vec3(0, 1.5f, 0), glm::vec3(1.0f, 0.7f, 0.3f) * 2.0f, 12.0f });
        localLights.push_back({ glm::vec3(cosf(a) * 18.0f, H * 2.0f - 2.0f, sinf(a) * 18.0f), coolLight * 1.3f, 26.0f });
    }

    // 8방향 선로. 높이를 모두 달리 해서 서로, 그리고 발코니와 부딪히지 않는다
    tracks.clear();
    const float hs[8] = { 4.0f, 9.0f, 16.0f, 21.0f, 28.5f, 33.5f, 41.0f, 46.0f };
    const int dirOrder[8] = { 0, 5, 2, 7, 4, 1, 6, 3 };
    for (int k = 0; k < 8; ++k) {
        float th = dirOrder[k] * PI * 0.25f;
        glm::vec3 d(cosf(th), 0.0f, sinf(th));
        if (fabsf(d.x) < 1e-4f) d.x = 0.0f;
        if (fabsf(d.z) < 1e-4f) d.z = 0.0f;
        glm::vec3 perp(-d.z, 0.0f, d.x);
        Track t;
        t.dir = d;
        t.c = perp * (((k * 3) % 5 - 2) * 2.5f) + glm::vec3(0, hs[k], 0);
        t.sMin = -1e9f;
        t.sMax = 1e9f;
        for (int i : { 0, 2 }) {
            if (fabsf(d[i]) < 1e-3f) continue;
            float a = ((H - 0.3f) - t.c[i]) / d[i], b = (-(H - 0.3f) - t.c[i]) / d[i];
            t.sMax = std::min(t.sMax, std::max(a, b));
            t.sMin = std::max(t.sMin, std::min(a, b));
        }
        t.speed = 11.0f + (k % 3) * 2.5f;
        t.cars = 4 + k % 2;
        // 시작할 때 기차들이 이미 방 곳곳을 달리고 있도록 출발 위치를 흩어 둔다
        float period = (t.sMax - t.sMin) + t.cars * (CAR_LEN + CAR_GAP) + 30.0f;
        t.phase = fmodf(k * 0.618f, 1.0f) * (t.sMax - t.sMin) * 0.8f + t.cars * (CAR_LEN + CAR_GAP);
        t.phase = fmodf(t.phase, period);
        tracks.push_back(t);
        // 고가 선로(콘크리트 들보) + 레일 두 줄 + 아래 조명
        float mid = (t.sMin + t.sMax) * 0.5f, len = t.sMax - t.sMin + 1.0f;
        localDecor.push_back({ trackFrame(t, mid, -0.45f) * glm::scale(glm::mat4(1.0f), glm::vec3(len, 0.8f, 3.4f)), conc });
        for (int s = -1; s <= 1; s += 2)
            localDecor.push_back({ trackFrame(t, mid, 0.02f) * TRS(glm::vec3(0, 0, s * 0.75f), glm::vec3(len, 0.14f, 0.14f)), railM });
        localDecor.push_back({ trackFrame(t, mid, -0.9f) * glm::scale(glm::mat4(1.0f), glm::vec3(len, 0.06f, 0.3f)), lamp });
    }
}

// 각 면 중앙은 포털 자리라 비워 둔다. 90도 회전은 면 중앙을 면 중앙으로 보내므로 어느 방향으로 돌아도 맞다
void clearPortalZones() {
    localBoxes.erase(std::remove_if(localBoxes.begin(), localBoxes.end(), [](const Box& b) {
        if (b.shellDir != glm::vec3(0.0f)) return false;
        glm::vec3 p = b.center - ROOM_C;
        for (const auto& d : FACE_DIRS) {
            float along = glm::dot(p, d);
            glm::vec3 lat = glm::abs(p - d * along);
            if (along > ROOM_HALF - 13.0f && std::max({ lat.x, lat.y, lat.z }) < 7.0f) return true;
        }
        return false;
    }), localBoxes.end());
}

// 칸 번호로부터 방을 만든다 (같은 칸은 언제나 같은 모양)
void buildLocalWorld(int cell) {
    localBoxes.clear();
    localDecor.clear();
    localLights.clear();
    tracks.clear();
    if (cell == HUB_CELL) { buildHub(); return; }
    bool arena = cell == ARENA_CELL;
    const Theme& t = THEMES[arena ? 0 : cell % 6];
    std::mt19937 r(7919u * (unsigned)cell + 13u);
    for (int f = 0; f < 6; ++f) {
        glm::vec3 d = FACE_DIRS[f], ad = glm::abs(d);
        glm::vec3 base = arena && f == 3 ? M_FLOOR.color : t.wall;
        Material m{ glm::mix(base, FACE_COLORS[f], f == 3 ? 0.18f : 0.42f), 0.2f, 24.0f, 0.0f, 1,
                    f == 3 ? TEX_TILE : TEX_PANEL, f == 3 ? 6.0f : 12.0f };
        Box shell{ ROOM_C + d * (ROOM_HALF + 0.5f), ad + (glm::vec3(1.0f) - ad) * (ROOM_HALF * 2.0f + 2.0f), m, true };
        shell.shellDir = d;
        localBoxes.push_back(shell);
    }
    for (int f = 0; f < 6; ++f)
        if (!(arena && f == 3)) addFaceProps(f, r, t);
    if (arena) buildArena();
    clearPortalZones();
}

// 90도 회전이므로 축 정렬 상자는 회전해도 축 정렬 상자다. 충돌/중력 코드를 그대로 쓸 수 있는 이유
void rebuildWorld() {
    boxes.clear();
    for (Box b : localBoxes) {
        b.center = ROOM_C + worldOrient * (b.center - ROOM_C);
        b.size = glm::abs(worldOrient * b.size);
        b.shellDir = worldOrient * b.shellDir;
        boxes.push_back(b);
    }
}

// ===================================================================
// 충돌
// ===================================================================
bool segAABB(glm::vec3 p0, glm::vec3 p1, glm::vec3 bmin, glm::vec3 bmax, float& tHit) {
    glm::vec3 d = p1 - p0;
    float tmin = 0.0f, tmax = 1.0f;
    for (int i = 0; i < 3; ++i) {
        if (fabsf(d[i]) < 1e-8f) {
            if (p0[i] < bmin[i] || p0[i] > bmax[i]) return false;
        } else {
            float inv = 1.0f / d[i];
            float t1 = (bmin[i] - p0[i]) * inv, t2 = (bmax[i] - p0[i]) * inv;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }
    }
    tHit = tmin;
    return true;
}

// 선분이 세로 원기둥(캐릭터 몸통)을 지나는지
bool segHitsCylinder(glm::vec3 p0, glm::vec3 p1, glm::vec3 base, float r, float h) {
    glm::vec2 a(p0.x, p0.z), b(p1.x, p1.z), c(base.x, base.z);
    glm::vec2 ab = b - a;
    float len2 = glm::dot(ab, ab);
    float t = len2 > 1e-8f ? glm::clamp(glm::dot(c - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
    if (glm::length(a + ab * t - c) > r) return false;
    float y = p0.y + (p1.y - p0.y) * t;
    return y >= base.y && y <= base.y + h;
}

bool segmentBlocked(glm::vec3 p0, glm::vec3 p1, float* tOut = nullptr) {
    float best = 2.0f;
    for (const auto& b : boxes) {
        if (!b.solid) continue;
        float t;
        if (segAABB(p0, p1, b.bmin(), b.bmax(), t) && t < best) best = t;
    }
    if (p1.y < 0.0f && p0.y >= 0.0f) best = std::min(best, p0.y / (p0.y - p1.y));
    if (tOut) *tOut = best;
    return best <= 1.0f;
}

void pushOutCircle(glm::vec3& p, float r, const Box& b) {
    glm::vec3 mn = b.bmin(), mx = b.bmax();
    float cx = glm::clamp(p.x, mn.x, mx.x), cz = glm::clamp(p.z, mn.z, mx.z);
    float dx = p.x - cx, dz = p.z - cz;
    float d2 = dx * dx + dz * dz;
    if (d2 > r * r) return;
    if (d2 > 1e-8f) {
        float d = sqrtf(d2);
        p.x += dx / d * (r - d);
        p.z += dz / d * (r - d);
    } else {
        float l = p.x - mn.x, rr = mx.x - p.x, bk = p.z - mn.z, f = mx.z - p.z;
        float m = std::min({ l, rr, bk, f });
        if (m == l) p.x = mn.x - r;
        else if (m == rr) p.x = mx.x + r;
        else if (m == bk) p.z = mn.z - r;
        else p.z = mx.z + r;
    }
}

bool circleOverlapsBoxXZ(glm::vec3 p, float r, const Box& b) {
    glm::vec3 mn = b.bmin(), mx = b.bmax();
    float cx = glm::clamp(p.x, mn.x, mx.x), cz = glm::clamp(p.z, mn.z, mx.z);
    return (p.x - cx) * (p.x - cx) + (p.z - cz) * (p.z - cz) < r * r;
}

float groundHeightAt(glm::vec3 p, float r) {
    float ground = 0.0f;
    for (const auto& b : boxes) {
        if (!b.solid) continue;
        float top = b.bmax().y;
        if (top <= p.y + STEP_H && circleOverlapsBoxXZ(p, r, b)) ground = std::max(ground, top);
    }
    return ground;
}

// 바닥 위에서 구조물과 겹치지 않는 지점
bool spotIsFree(glm::vec3 p, float r) {
    for (const auto& b : boxes)
        if (b.solid && b.bmax().y > 0.1f && b.bmin().y < 2.2f && circleOverlapsBoxXZ(p, r, b)) return false;
    return true;
}

// ===================================================================
// 게임 로직
// ===================================================================
glm::vec3 forwardDir() {
    return glm::vec3(sinf(player.yaw) * cosf(player.pitch), sinf(player.pitch), -cosf(player.yaw) * cosf(player.pitch));
}

glm::vec3 eyePosition() {
    float bob = player.onGround ? sinf(player.bobPhase * 2.0f) * 0.035f : 0.0f;
    glm::vec3 e = player.pos + glm::vec3(0, EYE_H - player.landDip + bob, 0);
    if (player.dead) e.y = player.pos.y + std::max(0.4f, EYE_H - player.deadT * 1.2f);
    return e;
}

void spawnSparks(glm::vec3 p, glm::vec3 color, int n, float speed) {
    for (int i = 0; i < n; ++i)
        sparks.push_back({ p, glm::vec3(frand(-1, 1), frand(0, 1.5f), frand(-1, 1)) * speed, color, frand(0.2f, 0.5f) });
}

void spawnWave() {
    ++wave;
    waveBannerT = 2.5f;
    int count = std::min(2 + wave, 8);
    for (int i = 0; i < count; ++i) {
        // 방이 회전하면 구조물 위치가 바뀌므로 빈 자리를 찾아 스폰
        glm::vec3 spot(0.0f);
        bool found = false;
        for (int attempt = 0; attempt < 60 && !found; ++attempt) {
            spot = glm::vec3(frand(-ROOM_HALF + 3.0f, ROOM_HALF - 3.0f), 0, frand(-ROOM_HALF + 3.0f, ROOM_HALF - 3.0f));
            found = glm::length(spot - player.pos) > 12.0f && spotIsFree(spot, 0.7f);
        }
        if (!found) continue;
        Enemy e{};
        e.pos = spot;
        e.shootCd = frand(1.5f, 3.0f);
        e.strafe = frand(0, 1) < 0.5f ? -1.0f : 1.0f;
        e.alive = true;
        enemies.push_back(e);
    }
}

void resetGame() {
    enemies.clear(); bullets.clear(); shards.clear(); sparks.clear();
    player = Player{};
    player.pos = glm::vec3(0, 0, 4);
    for (int attempt = 0; attempt < 60 && !spotIsFree(player.pos, 0.6f); ++attempt)
        player.pos = glm::vec3(frand(-18, 18), 0, frand(-18, 18));
    player.hp = 3;
    player.onGround = true;
    wave = 0; kills = 0;
    waveTimer = 1.0f;
    timeScale = 1.0f;
    focusEnergy = 1.0f;
}

// ===================================================================
// 웹스윙(그래플)과 기차 매달리기
// ===================================================================
struct Grapple {
    bool active = false, onTrain = false;
    glm::vec3 anchor{ 0.0f };   // 줄이 걸린 점 (월드)
    float len = 0.0f;           // 줄 길이. 당기면서 줄어든다
    int track = -1, car = 0;
    float side = 1.0f;          // 객차의 어느 옆면에 매달렸는지
} grap;
bool shiftWasDown = false, testShift = false, grappleAimOK = false;
const float GRAPPLE_RANGE = 70.0f;

glm::mat4 carWorld(int k, int i) {
    return roomToWorld() * trackFrame(tracks[k], carS(tracks[k], i), CAR_H * 0.5f + 0.1f);
}

// 시선 광선이 처음 닿는 곳: 달리는 객차(회전된 상자) 또는 고정 구조물
bool castGrapple(glm::vec3 o, glm::vec3 d, Grapple& out) {
    float best = 2.0f, tw;
    glm::vec3 p1 = o + d * GRAPPLE_RANGE;
    if (segmentBlocked(o, p1, &tw) && tw <= 1.0f) {
        best = tw;
        out = Grapple{};
        out.active = true;
        out.anchor = o + (p1 - o) * tw;
    }
    for (int k = 0; k < (int)tracks.size(); ++k)
        for (int i = 0; i < tracks[k].cars; ++i) {
            if (!carVisible(tracks[k], carS(tracks[k], i))) continue;
            // 광선을 객차의 단위 상자 좌표계로 옮기면(역행렬) 축 정렬 상자 검사를 그대로 쓸 수 있다
            glm::mat4 inv = glm::inverse(carWorld(k, i) * glm::scale(glm::mat4(1.0f), glm::vec3(CAR_LEN, CAR_H, CAR_W)));
            glm::vec3 a = glm::vec3(inv * glm::vec4(o, 1.0f)), b = glm::vec3(inv * glm::vec4(p1, 1.0f));
            float t;
            if (segAABB(a, b, glm::vec3(-0.5f), glm::vec3(0.5f), t) && t < best) {
                best = t;
                out = Grapple{};
                out.active = out.onTrain = true;
                out.track = k;
                out.car = i;
                out.anchor = o + (p1 - o) * t;
            }
        }
    return best <= 1.0f;
}

void releaseGrapple() { grap = Grapple{}; }

void tryGrapple() {
    Grapple g;
    if (!castGrapple(eyePosition(), forwardDir(), g)) return;
    g.len = std::max(2.5f, glm::length(player.pos + glm::vec3(0, 1.5f, 0) - g.anchor));
    if (g.onTrain) {
        glm::mat4 C = carWorld(g.track, g.car);
        g.side = glm::dot(glm::vec3(C[2]), player.pos - glm::vec3(C[3])) >= 0.0f ? 1.0f : -1.0f;
    }
    grap = g;
    player.onGround = false;
}

// ===================================================================
// 방 이동: 포털과 기차
// ===================================================================
bool portalArmed = true;        // 도착 직후 같은 포털로 되돌아가지 않도록, 포털 밖으로 한 번 나가야 다시 켜진다
float bannerT = 0.0f;
std::string bannerText;

bool portalsOn() { return currentCell != HUB_CELL; }

// 지금 방에서 월드 방향 D(축 방향)의 벽 너머에 붙어 있는 칸. 큐브를 돌리면 결과가 바뀐다
int portalTarget(glm::vec3 D) {
    glm::ivec3 p = cubies[currentCell].pos + glm::ivec3(glm::round(D));
    if (std::abs(p.x) > 1 || std::abs(p.y) > 1 || std::abs(p.z) > 1) return -1;
    return cubieAt(p);
}

// D 방향으로 나가서 칸 id 로 들어간다. 큐브 방향과 월드 방향이 같으므로 반대쪽(-D) 벽 근처에서 나타난다
void travelTo(int id, glm::vec3 D) {
    releaseGrapple();
    currentCell = id;
    worldOrient = cubies[id].orient;
    buildLocalWorld(id);
    rebuildWorld();
    enemies.clear(); bullets.clear(); shards.clear(); sparks.clear();
    wave = 0;
    waveTimer = 1.5f;
    const float H = ROOM_HALF;
    if (glm::length(D) < 0.5f) {
        player.pos = glm::vec3(0, 0, 4);
        player.vel = glm::vec3(0.0f);
    } else if (fabsf(D.y) > 0.5f) {
        // 천장으로 나가면 위 방의 바닥에서, 바닥으로 나가면 아래 방의 천장에서 나타난다
        player.pos = D.y > 0.0f ? glm::vec3(0, 0.2f, 7.0f) : glm::vec3(0, H * 2.0f - 4.0f, 7.0f);
        player.vel = glm::vec3(0, D.y > 0.0f ? 9.0f : 0.0f, 0);
    } else {
        glm::vec3 h = glm::normalize(glm::vec3(D.x, 0, D.z));
        float y = glm::clamp(player.pos.y, 0.5f, H * 2.0f - 4.0f);
        player.pos = -h * (H - 3.5f) + glm::vec3(0, y, 0);
        player.vel = h * 7.0f + glm::vec3(0, 3.0f, 0);
        player.yaw = atan2f(h.x, -h.z);
    }
    player.onGround = false;
    portalArmed = false;
    bannerText = id == HUB_CELL ? "HUB" : "ROOM " + std::to_string(id);
    bannerT = 2.5f;
}

void restartRun() {
    initCube();
    for (bool& c : roomCleared) c = false;
    roomCleared[HUB_CELL] = true;
    currentCell = HUB_CELL;
    worldOrient = glm::mat3(1.0f);
    buildLocalWorld(currentCell);
    rebuildWorld();
    resetGame();
    releaseGrapple();
    portalArmed = true;
    bannerText = "HUB - SHIFT TO GRAPPLE A TRAIN";
    bannerT = 4.0f;
}

void updatePortals() {
    if (!portalsOn() || player.dead) return;
    glm::vec3 p = player.pos + glm::vec3(0, 0.9f, 0) - ROOM_C;
    bool inside = false;
    for (const auto& D : FACE_DIRS) {
        float along = glm::dot(p, D);
        glm::vec3 lat = glm::abs(p - D * along);
        if (along < ROOM_HALF - 1.6f || std::max({ lat.x, lat.y, lat.z }) > 4.2f) continue;
        inside = true;
        int target = portalTarget(D);
        if (portalArmed && roomCleared[currentCell] && target >= 0) { travelTo(target, D); return; }
    }
    if (!inside) portalArmed = true;
}

// 기차에 매달려 가다가 객차가 벽(포털) 속으로 들어가면 선로가 향하는 칸으로 넘어간다
void updateTrainRide(float dt) {
    const Track& t = tracks[grap.track];
    float s = carS(t, grap.car);
    if (!carVisible(t, s)) { releaseGrapple(); return; }
    glm::mat4 C = carWorld(grap.track, grap.car);
    glm::vec3 center = glm::vec3(C[3]), side = glm::normalize(glm::vec3(C[2])) * grap.side;
    glm::vec3 target = center + side * (CAR_W * 0.5f + 0.45f) - glm::vec3(0, 1.4f, 0);
    player.pos += (target - player.pos) * std::min(1.0f, dt * 9.0f);
    player.vel = worldOrient * t.dir * t.speed;
    player.onGround = false;
    grap.anchor = center + side * (CAR_W * 0.5f) + glm::vec3(0, 0.6f, 0);
    if (s > t.sMax - CAR_LEN * 0.5f) {
        glm::vec3 D = worldOrient * glm::round(t.dir);     // 대각선 선로면 (±1, 0, ±1)
        int dest = cubieAt(cubies[currentCell].pos + glm::ivec3(glm::round(D)));
        if (dest >= 0) travelTo(dest, D);
        else releaseGrapple();
    }
}

// 적 몸체 각 부위의 월드 행렬 (그리기/총구/파편 생성 공용)
struct EnemyRig { glm::mat4 root, legs[2], arms[2], torso, head; };

EnemyRig enemyRig(const Enemy& e) {
    EnemyRig r;
    float rise = glm::clamp(e.spawnT, 0.0f, 1.0f);
    r.root = glm::translate(glm::mat4(1.0f), e.pos + glm::vec3(0, (rise - 1.0f) * 2.0f, 0));
    r.root = glm::rotate(r.root, e.yaw, glm::vec3(0, 1, 0));
    float swing = sinf(e.walkT * 7.0f) * 0.55f;
    for (int i = 0; i < 2; ++i) {
        float s = i == 0 ? -1.0f : 1.0f;
        r.legs[i] = glm::rotate(glm::translate(r.root, glm::vec3(0.15f * s, 0.9f, 0)), swing * s, glm::vec3(1, 0, 0));
    }
    r.arms[0] = glm::rotate(glm::translate(r.root, glm::vec3(-0.37f, 1.55f, 0)), -swing * 0.7f, glm::vec3(1, 0, 0));
    float aim = -PI * 0.5f * glm::clamp(e.aimT, 0.3f, 1.0f);
    r.arms[1] = glm::rotate(glm::translate(r.root, glm::vec3(0.37f, 1.55f, 0)), aim, glm::vec3(1, 0, 0));
    r.torso = glm::translate(r.root, glm::vec3(0, 1.25f, 0));
    r.head = glm::translate(r.root, glm::vec3(0, 1.82f, 0));
    return r;
}

glm::vec3 enemyMuzzle(const Enemy& e) {
    EnemyRig r = enemyRig(e);
    return glm::vec3(r.arms[1] * glm::vec4(0, -0.92f, 0.04f, 1));
}

void killEnemy(Enemy& e, glm::vec3 hitDir) {
    e.alive = false;
    ++kills;
    EnemyRig r = enemyRig(e);
    for (int i = 0; i < 46; ++i) {
        glm::vec3 local(frand(-0.28f, 0.28f), frand(0.1f, 1.95f), frand(-0.17f, 0.17f));
        Shard s;
        s.pos = glm::vec3(r.root * glm::vec4(local, 1));
        s.vel = hitDir * frand(2.0f, 7.0f) + glm::vec3(frand(-2, 2), frand(0.5f, 4.5f), frand(-2, 2));
        s.axis = glm::normalize(glm::vec3(frand(-1, 1), frand(-1, 1), frand(-1, 1)) + glm::vec3(0.001f));
        s.angle = frand(0, 6.28f);
        s.angVel = frand(3, 14);
        s.size = frand(0.08f, 0.24f);
        s.life = frand(6, 9);
        s.glow = i < 5;
        s.color = s.glow ? M_VISOR.color : (frand(0, 1) < 0.5f ? M_ENEMY.color : M_ENEMY_DARK.color);
        shards.push_back(s);
    }
    if (shards.size() > 700) shards.erase(shards.begin(), shards.begin() + (shards.size() - 700));
}

void hurtPlayer() {
    if (player.dead) return;
    --player.hp;
    player.hurtT = 1.0f;
    if (player.hp <= 0) {
        player.hp = 0;
        player.dead = true;
        player.deadT = 0.0f;
    }
}

void firePlayer() {
    glm::vec3 eye = eyePosition(), fwd = forwardDir();
    glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
    glm::vec3 up = glm::cross(right, fwd);
    glm::vec3 muzzle = eye + right * 0.2f - up * 0.16f + fwd * 0.7f;
    float tWall;
    glm::vec3 far = eye + fwd * 80.0f;
    segmentBlocked(eye, far, &tWall);
    glm::vec3 aim = eye + fwd * (80.0f * std::min(tWall, 1.0f));
    bullets.push_back({ muzzle, glm::normalize(aim - muzzle) * 75.0f, true, true, 3.0f });
    player.fireCd = 0.22f;
    player.recoil = 1.0f;
    player.muzzleT = 0.05f;
}

void updatePlayer(float dt) {
    player.hurtT = std::max(0.0f, player.hurtT - dt * 1.5f);
    player.landDip = std::max(0.0f, player.landDip - dt * 1.2f);
    player.recoil = std::max(0.0f, player.recoil - dt * 7.0f);
    player.fireCd -= dt;
    player.muzzleT -= dt;
    if (player.dead) { player.deadT += dt; return; }

    glm::vec3 f(sinf(player.yaw), 0, -cosf(player.yaw)), r(cosf(player.yaw), 0, sinf(player.yaw));
    glm::vec3 wish(0.0f);
    if (glfwGetKey(gWindow, GLFW_KEY_W) == GLFW_PRESS) wish += f;
    if (glfwGetKey(gWindow, GLFW_KEY_S) == GLFW_PRESS) wish -= f;
    if (glfwGetKey(gWindow, GLFW_KEY_D) == GLFW_PRESS) wish += r;
    if (glfwGetKey(gWindow, GLFW_KEY_A) == GLFW_PRESS) wish -= r;
    float speed = glfwGetKey(gWindow, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ? 8.5f : 5.5f;
    if (glm::length(wish) > 0.01f) wish = glm::normalize(wish) * speed;

    // Shift 를 누르는 순간 줄을 쏘고, 떼면 놓는다
    bool shift = glfwGetKey(gWindow, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || testShift;
    if (shift && !shiftWasDown) tryGrapple();
    if (!shift && grap.active) releaseGrapple();
    shiftWasDown = shift;
    if (grap.active && grap.onTrain) {
        updateTrainRide(dt);
        return;
    }

    glm::vec2 hv(player.vel.x, player.vel.z), target(wish.x, wish.z);
    if (player.onGround) {
        hv += (target - hv) * std::min(1.0f, 14.0f * dt);
    } else if (glm::length(target) > 0.01f) {
        // 공중에서는 원하는 방향으로만 가속하고 속도를 깎지 않는다 → 스윙 관성이 유지된다
        glm::vec2 wd = glm::normalize(target);
        float add = speed - glm::dot(hv, wd);
        if (add > 0.0f) hv += wd * std::min(add, 12.0f * dt);
    }
    player.vel.x = hv.x;
    player.vel.z = hv.y;
    if (player.onGround && glm::length(hv) > 0.5f) player.bobPhase += dt * glm::length(hv) * 1.5f;

    if (player.onGround && glfwGetKey(gWindow, GLFW_KEY_SPACE) == GLFW_PRESS) {
        player.vel.y = 6.5f;
        player.onGround = false;
    }
    player.vel.y = std::max(player.vel.y - 18.0f * dt, -30.0f);

    player.pos += player.vel * dt;
    if (grap.active) {
        // 줄 제약: 손과 고정점 거리가 줄 길이를 넘지 못한다. 줄을 감으면서 진자처럼 흔들린다
        grap.len = std::max(2.5f, grap.len - 9.0f * dt);
        glm::vec3 d = player.pos + glm::vec3(0, 1.5f, 0) - grap.anchor;
        float L = glm::length(d);
        if (L > grap.len) {
            glm::vec3 n = d / L;
            player.pos -= n * (L - grap.len);
            float vr = glm::dot(player.vel, n);
            if (vr > 0.0f) player.vel -= n * vr;
        }
    }
    for (const auto& b : boxes) {
        if (!b.solid) continue;
        if (b.bmax().y > player.pos.y + STEP_H && b.bmin().y < player.pos.y + PLAYER_H)
            pushOutCircle(player.pos, PLAYER_R, b);
    }
    player.pos.x = glm::clamp(player.pos.x, -ROOM_HALF + PLAYER_R, ROOM_HALF - PLAYER_R);
    player.pos.z = glm::clamp(player.pos.z, -ROOM_HALF + PLAYER_R, ROOM_HALF - PLAYER_R);
    if (player.pos.y > ROOM_HALF * 2.0f - PLAYER_H - 0.05f) {
        player.pos.y = ROOM_HALF * 2.0f - PLAYER_H - 0.05f;
        player.vel.y = std::min(player.vel.y, 0.0f);
    }

    float ground = groundHeightAt(player.pos, PLAYER_R * 0.7f);
    bool wasGround = player.onGround;
    if (player.pos.y <= ground + 0.001f || (!grap.active && wasGround && player.vel.y <= 0.0f && player.pos.y - ground < 0.25f)) {
        if (!wasGround && player.vel.y < -6.0f) player.landDip = 0.18f;
        player.pos.y = ground;
        player.vel.y = 0.0f;
        player.onGround = true;
    } else {
        player.onGround = false;
    }

    bool fire = glfwGetMouseButton(gWindow, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    if (fire && player.fireCd <= 0.0f) firePlayer();
}

void updateEnemies(float dt) {
    glm::vec3 head = player.pos + glm::vec3(0, 1.45f, 0);
    for (auto& e : enemies) {
        if (!e.alive) continue;
        if (e.spawnT < 1.0f) { e.spawnT += dt * 0.8f; continue; }

        // 방이 회전하면 적도 새 바닥으로 떨어진다
        e.vy = std::max(e.vy - 18.0f * dt, -30.0f);
        e.pos.y += e.vy * dt;
        float eg = groundHeightAt(e.pos, ENEMY_R * 0.7f);
        if (e.pos.y <= eg) { e.pos.y = eg; e.vy = 0.0f; }

        glm::vec3 toP = player.pos - e.pos;
        toP.y = 0;
        float dist = glm::length(toP);
        glm::vec3 dir = dist > 0.01f ? toP / dist : glm::vec3(0, 0, 1);
        e.yaw = atan2f(dir.x, dir.z);

        glm::vec3 move(0.0f);
        if (dist > 11.0f) move = dir;
        else if (dist < 6.0f) move = -dir;
        else move = glm::vec3(-dir.z, 0, dir.x) * e.strafe * 0.7f;
        if (frand(0, 1) < dt * 0.4f) e.strafe = -e.strafe;
        if (glm::length(move) > 0.01f && !player.dead) {
            e.pos += glm::normalize(move) * 2.3f * dt;
            e.walkT += dt;
        }
        for (const auto& b : boxes)
            if (b.solid && b.bmax().y > e.pos.y + STEP_H && b.bmin().y < e.pos.y + ENEMY_H) pushOutCircle(e.pos, ENEMY_R, b);

        glm::vec3 muzzle = enemyMuzzle(e);
        bool see = !segmentBlocked(muzzle, head);
        e.aimT = glm::clamp(e.aimT + (see ? dt * 2.0f : -dt), 0.0f, 1.0f);
        e.shootCd -= dt;
        if (e.shootCd <= 0.0f && see && !player.dead && e.aimT > 0.8f) {
            glm::vec3 aim = player.pos + glm::vec3(0, 1.25f, 0) + glm::vec3(frand(-0.4f, 0.4f), frand(-0.3f, 0.3f), frand(-0.4f, 0.4f));
            bullets.push_back({ muzzle, glm::normalize(aim - muzzle) * 15.0f, false, true, 6.0f });
            e.shootCd = frand(1.8f, 3.2f);
            spawnSparks(muzzle, glm::vec3(1.0f, 0.55f, 0.15f), 4, 1.5f);
        }
    }

    for (size_t i = 0; i < enemies.size(); ++i)
        for (size_t j = i + 1; j < enemies.size(); ++j) {
            Enemy& a = enemies[i];
            Enemy& b = enemies[j];
            if (!a.alive || !b.alive) continue;
            glm::vec3 d = b.pos - a.pos;
            d.y = 0;
            float len = glm::length(d);
            if (len > 0.001f && len < ENEMY_R * 2.2f) {
                glm::vec3 push = d / len * (ENEMY_R * 2.2f - len) * 0.5f;
                a.pos -= push;
                b.pos += push;
            }
        }
    enemies.erase(std::remove_if(enemies.begin(), enemies.end(), [](const Enemy& e) { return !e.alive; }), enemies.end());
}

void updateBullets(float dt) {
    for (auto& b : bullets) {
        if (!b.alive) continue;
        b.life -= dt;
        glm::vec3 p0 = b.pos, p1 = b.pos + b.vel * dt;

        float tWall;
        bool wall = segmentBlocked(p0, p1, &tWall);
        float tHitT = wall ? tWall : 1.0f;
        glm::vec3 dir = glm::normalize(b.vel);

        if (b.fromPlayer) {
            for (auto& e : enemies) {
                if (!e.alive || e.spawnT < 0.5f) continue;
                glm::vec3 end = p0 + (p1 - p0) * tHitT;
                if (segHitsCylinder(p0, end, e.pos, ENEMY_R, ENEMY_H)) {
                    killEnemy(e, dir);
                    b.alive = false;
                    break;
                }
            }
        } else if (!player.dead) {
            glm::vec3 end = p0 + (p1 - p0) * tHitT;
            if (segHitsCylinder(p0, end, player.pos, PLAYER_R, PLAYER_H)) {
                hurtPlayer();
                b.alive = false;
            }
        }
        if (b.alive && wall) {
            b.alive = false;
            spawnSparks(p0 + (p1 - p0) * tWall - dir * 0.05f, b.fromPlayer ? glm::vec3(0.7f, 0.95f, 1.0f) : glm::vec3(1.0f, 0.5f, 0.15f), 6, 3.0f);
        }
        b.pos = p1;
        if (b.life <= 0.0f) b.alive = false;
    }
    bullets.erase(std::remove_if(bullets.begin(), bullets.end(), [](const Bullet& b) { return !b.alive; }), bullets.end());
}

void updateDebris(float dt) {
    for (auto& s : shards) {
        s.life -= dt;
        s.vel.y -= 9.8f * dt;
        s.pos += s.vel * dt;
        s.angle += s.angVel * dt;
        float floorY = s.size * 0.35f;
        if (s.pos.y < floorY) {
            s.pos.y = floorY;
            s.vel.y *= -0.3f;
            s.vel.x *= 0.7f;
            s.vel.z *= 0.7f;
            s.angVel *= 0.6f;
        }
    }
    shards.erase(std::remove_if(shards.begin(), shards.end(), [](const Shard& s) { return s.life <= 0.0f; }), shards.end());
    for (auto& s : sparks) {
        s.life -= dt;
        s.vel.y -= 9.8f * dt;
        s.pos += s.vel * dt;
    }
    sparks.erase(std::remove_if(sparks.begin(), sparks.end(), [](const Spark& s) { return s.life <= 0.0f; }), sparks.end());
}

// ===================================================================
// 큐브 화면 (Tab): 드래그로 큐브 전체 회전 / 한 면 회전
// ===================================================================
bool cubeView = false;
glm::quat cubeViewQ(1.0f, 0.0f, 0.0f, 0.0f);
const int DRAG_NONE = 0, DRAG_CUBE = 1, DRAG_PENDING = 2, DRAG_SLICE = 3, DRAG_SNAP = 4;
int dragMode = DRAG_NONE;
double dragStartX = 0.0, dragStartY = 0.0, dragLastX = 0.0, dragLastY = 0.0, lastClickTime = -10.0;
glm::vec3 grabP(0.0f);
int grabNormalAxis = 0, lastClickCell = -1;
glm::ivec3 grabCell(0);
int sliceAxis = -1, sliceLayer = 0;
float sliceAngle = 0.0f, sliceTarget = 0.0f;
glm::vec2 sliceScreenVel(0.0f);
int hoverCell = -1, hoverAxis = -1, hoverLayer = 0;

// 월드 회전 연출
bool rotAnimActive = false;
float rotAnimT = 0.0f;
glm::quat rotAnimFrom(1.0f, 0.0f, 0.0f, 0.0f);
glm::vec3 preTopDir(0, 1, 0);

glm::mat4 cubeModel() { return glm::mat4_cast(cubeViewQ); }

glm::mat4 cubeViewProj() {
    int w, h;
    glfwGetFramebufferSize(gWindow, &w, &h);
    return glm::perspective(glm::radians(35.0f), (float)w / (float)std::max(h, 1), 0.1f, 50.0f) *
           glm::lookAt(glm::vec3(0, 0, 9.5f), glm::vec3(0.0f), glm::vec3(0, 1, 0));
}

// 큐브 공간의 점 → 창 좌표 (마우스와 같은 y 아래 방향)
glm::vec2 cubeToWindow(glm::vec3 p) {
    int ww, wh;
    glfwGetWindowSize(gWindow, &ww, &wh);
    glm::vec4 c = cubeViewProj() * cubeModel() * glm::vec4(p, 1.0f);
    return glm::vec2((c.x / c.w * 0.5f + 0.5f) * ww, (0.5f - c.y / c.w * 0.5f) * wh);
}

// 마우스 광선과 큐브 겉면 교차. 큐브 공간의 교차점과 면 축을 돌려준다
bool pickCube(double mx, double my, glm::vec3& hit, int& normalAxis) {
    int ww, wh;
    glfwGetWindowSize(gWindow, &ww, &wh);
    if (ww <= 0 || wh <= 0) return false;
    glm::mat4 inv = glm::inverse(cubeViewProj() * cubeModel());
    float nx = (float)(2.0 * mx / ww - 1.0), ny = (float)(1.0 - 2.0 * my / wh);
    glm::vec4 a = inv * glm::vec4(nx, ny, -1, 1), b = inv * glm::vec4(nx, ny, 1, 1);
    glm::vec3 o = glm::vec3(a) / a.w, d = glm::vec3(b) / b.w - o;
    float tmin = -1e9f, tmax = 1e9f;
    int axis = -1;
    for (int i = 0; i < 3; ++i) {
        if (fabsf(d[i]) < 1e-7f) {
            if (fabsf(o[i]) > 1.5f) return false;
            continue;
        }
        float t1 = (-1.5f - o[i]) / d[i], t2 = (1.5f - o[i]) / d[i];
        if (t1 > t2) std::swap(t1, t2);
        if (t1 > tmin) { tmin = t1; axis = i; }
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return false;
    }
    if (axis < 0 || tmin < 0.0f) return false;
    hit = o + d * tmin;
    normalAxis = axis;
    return true;
}

glm::ivec3 cellFromHit(glm::vec3 p) {
    return glm::ivec3(glm::clamp(glm::floor(p + 1.5f), glm::vec3(0.0f), glm::vec3(2.0f))) - glm::ivec3(1);
}

// 커서가 칸 안에서 좌우로 치우치면 그 방향의 줄, 위아래로 치우치면 다른 방향의 줄을 강조
void updateHover(double mx, double my) {
    glm::vec3 p;
    int n;
    hoverCell = -1;
    hoverAxis = -1;
    if (!pickCube(mx, my, p, n)) return;
    glm::ivec3 c = cellFromHit(p);
    hoverCell = cubieAt(c);
    int t1 = (n + 1) % 3, t2 = (n + 2) % 3;
    hoverAxis = fabsf(p[t1] - c[t1]) >= fabsf(p[t2] - c[t2]) ? t2 : t1;
    hoverLayer = c[hoverAxis];
}

// 큐브 공간의 점이 axis 축으로 1라디안 돌 때 화면에서 움직이는 픽셀 수
glm::vec2 screenVelocity(glm::vec3 p, int axis) {
    glm::vec3 a(0.0f);
    a[axis] = 1.0f;
    return (cubeToWindow(p + glm::cross(a, p) * 0.01f) - cubeToWindow(p)) / 0.01f;
}

void cubeMouseDown(double mx, double my) {
    if (dragMode == DRAG_SNAP) return;
    dragStartX = dragLastX = mx;
    dragStartY = dragLastY = my;
    glm::vec3 p;
    int n;
    if (!pickCube(mx, my, p, n)) {
        dragMode = DRAG_CUBE;   // 큐브 바깥을 잡으면 큐브 전체 회전
        return;
    }
    grabP = p;
    grabNormalAxis = n;
    grabCell = cellFromHit(p);
    dragMode = DRAG_PENDING;
}

void cubeMouseMove(double mx, double my) {
    if (dragMode == DRAG_CUBE) {
        float dx = (float)(mx - dragLastX), dy = (float)(my - dragLastY);
        cubeViewQ = glm::normalize(glm::angleAxis(dy * 0.008f, glm::vec3(1, 0, 0)) *
                                   glm::angleAxis(dx * 0.008f, glm::vec3(0, 1, 0)) * cubeViewQ);
    } else if (dragMode == DRAG_PENDING || dragMode == DRAG_SLICE) {
        glm::vec2 total((float)(mx - dragStartX), (float)(my - dragStartY));
        if (dragMode == DRAG_PENDING && glm::length(total) > 6.0f) {
            // 끌기 시작한 방향과 가장 잘 맞는 회전축을 고른다
            int t1 = (grabNormalAxis + 1) % 3, t2 = (grabNormalAxis + 2) % 3;
            glm::vec2 s1 = screenVelocity(grabP, t1), s2 = screenVelocity(grabP, t2), dir = glm::normalize(total);
            float a1 = fabsf(glm::dot(glm::normalize(s1), dir)), a2 = fabsf(glm::dot(glm::normalize(s2), dir));
            sliceAxis = a1 >= a2 ? t1 : t2;
            sliceScreenVel = a1 >= a2 ? s1 : s2;
            sliceLayer = grabCell[sliceAxis];
            sliceAngle = 0.0f;
            dragMode = DRAG_SLICE;
        }
        if (dragMode == DRAG_SLICE) sliceAngle = glm::dot(total, sliceScreenVel) / glm::dot(sliceScreenVel, sliceScreenVel);
    }
    dragLastX = mx;
    dragLastY = my;
}

void enterCell(int id);
void closeCubeView();

void cubeMouseUp(double mx, double my) {
    if (dragMode == DRAG_SLICE) {
        sliceTarget = glm::round(sliceAngle / (PI * 0.5f)) * (PI * 0.5f);   // 가장 가까운 90도로 맞춤
        dragMode = DRAG_SNAP;
        return;
    }
    if (dragMode == DRAG_PENDING) {
        // 움직이지 않고 두 번 누르면 그 칸으로 이동
        int cell = cubieAt(grabCell);
        double now = glfwGetTime();
        bool twice = now - lastClickTime < 0.35 && cell == lastClickCell;
        lastClickTime = now;
        lastClickCell = cell;
        dragMode = DRAG_NONE;
        if (twice) enterCell(cell);
        return;
    }
    dragMode = DRAG_NONE;
}

void updateCubeView(float dt) {
    if (dragMode == DRAG_SNAP) {
        float diff = sliceTarget - sliceAngle, step = dt * 6.0f;
        if (fabsf(diff) <= step) {
            int turns = (int)glm::round(sliceTarget / (PI * 0.5f));
            if (turns != 0) turnSlice(sliceAxis, sliceLayer, turns);
            sliceAxis = -1;
            sliceAngle = 0.0f;
            dragMode = DRAG_NONE;
        } else {
            sliceAngle += diff > 0.0f ? step : -step;
        }
    }
    if (dragMode == DRAG_NONE) {
        double mx, my;
        glfwGetCursorPos(gWindow, &mx, &my);
        updateHover(mx, my);
    }
}

void openCubeView() {
    releaseGrapple();
    cubeView = true;
    dragMode = DRAG_NONE;
    sliceAxis = -1;
    glfwSetInputMode(gWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

// 현재 칸의 방향이 바뀌었으면 월드 전체를 그만큼 회전시킨다
void applyCubeToWorld() {
    glm::mat3 target = cubies[currentCell].orient;
    glm::mat3 r = target * glm::transpose(worldOrient);
    if (r == glm::mat3(1.0f)) return;
    worldOrient = target;
    rebuildWorld();

    auto rot = [&](glm::vec3 p) { return ROOM_C + r * (p - ROOM_C); };
    // 눈 위치를 기준으로 옮겨서 회전 연출이 끊기지 않게 한다
    glm::vec3 eye = rot(eyePosition());
    player.pos = eye - glm::vec3(0, EYE_H, 0);
    player.pos.x = glm::clamp(player.pos.x, -ROOM_HALF + 0.5f, ROOM_HALF - 0.5f);
    player.pos.z = glm::clamp(player.pos.z, -ROOM_HALF + 0.5f, ROOM_HALF - 0.5f);
    player.pos.y = glm::clamp(player.pos.y, 0.0f, ROOM_HALF * 2.0f - 2.5f);
    player.vel = glm::vec3(0.0f);
    player.onGround = false;
    for (auto& e : enemies) {
        e.pos = rot(e.pos);
        e.pos.x = glm::clamp(e.pos.x, -ROOM_HALF + 0.5f, ROOM_HALF - 0.5f);
        e.pos.z = glm::clamp(e.pos.z, -ROOM_HALF + 0.5f, ROOM_HALF - 0.5f);
        e.vy = 0.0f;
    }
    for (auto& b : bullets) { b.pos = rot(b.pos); b.vel = r * b.vel; }
    for (auto& s : shards) { s.pos = rot(s.pos); s.vel = r * s.vel; s.axis = r * s.axis; }
    for (auto& s : sparks) { s.pos = rot(s.pos); s.vel = r * s.vel; }

    rotAnimActive = true;
    rotAnimT = 0.0f;
    rotAnimFrom = glm::quat_cast(glm::transpose(r));
    preTopDir = r * glm::vec3(0, 1, 0);
}

void leaveCubeInput() {
    cubeView = false;
    dragMode = DRAG_NONE;
    sliceAxis = -1;
    glfwSetInputMode(gWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

void closeCubeView() {
    leaveCubeInput();
    applyCubeToWorld();
}

void enterCell(int id) {
    if (id == currentCell) { closeCubeView(); return; }
    leaveCubeInput();
    travelTo(id, glm::vec3(0.0f));     // 큐브 화면에서 바로 이동 (개발용)
}

void update(float realDt) {
    globalTime += realDt;
    if (cubeView) { updateCubeView(realDt); return; }
    if (rotAnimActive) {
        rotAnimT += realDt / 1.1f;
        if (rotAnimT >= 1.0f) rotAnimActive = false;
        return;
    }
    helpT -= realDt;
    waveBannerT -= realDt;
    bannerT -= realDt;

    // 집중 모드: 세계 시간은 크게 느려지고, 플레이어는 덜 느려짐
    focusActive = !player.dead && focusEnergy > 0.02f &&
                  glfwGetMouseButton(gWindow, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    if (focusActive) focusEnergy = std::max(0.0f, focusEnergy - realDt * 0.22f);
    else focusEnergy = std::min(1.0f, focusEnergy + realDt * 0.12f);
    float targetScale = player.dead ? 0.08f : (focusActive ? 0.1f : 1.0f);
    timeScale += (targetScale - timeScale) * std::min(1.0f, realDt * 8.0f);

    float worldDt = realDt * timeScale;
    float playerDt = realDt * (0.45f + 0.55f * timeScale);

    trainTime += worldDt;
    updatePlayer(playerDt);
    updatePortals();
    updateEnemies(worldDt);
    updateBullets(worldDt);
    updateDebris(worldDt);

    // 정해진 웨이브를 다 막으면 방 클리어 → 이 방의 포털이 열린다
    if (!roomCleared[currentCell] && enemies.empty() && !player.dead) {
        if (wave >= ROOM_WAVES) {
            roomCleared[currentCell] = true;
            bannerText = "ROOM CLEAR - PORTALS OPEN";
            bannerT = 3.0f;
        } else {
            waveTimer -= realDt;
            if (waveTimer <= 0.0f) {
                spawnWave();
                waveTimer = 2.0f;
            }
        }
    }
    Grapple probe;
    grappleAimOK = !grap.active && !player.dead && castGrapple(eyePosition(), forwardDir(), probe);
}

// ===================================================================
// 렌더링
// ===================================================================
GLuint sceneProg, shadowProg, skyProg, brightProg, blurProg, compositeProg, hudProg;
GLuint emptyVao, hudVao, hudVbo;

struct SceneLocs {
    GLint model, view, proj, lightVP, normalMat, objColor, Ks, shininess, emissive, pattern;
    GLint sunDir, sunColor, skyAmb, groundAmb, viewPos, fogColor, fogDensity, farPlane, shadowMap;
    GLint pointPos, pointColor, pointPower, alpha;
    GLint objScale, texLayer, texScale, albedoTex, numLights, lightPos, lightCol, lightRange;
} sl;
long long statDrawCalls = 0, statTriangles = 0;    // 성능 측정용 (프레임마다 0으로)
GLint shadowModelLoc, shadowVPLoc;

GLuint shadowFbo, shadowTex;
struct Targets {
    int w = 0, h = 0;
    GLuint hdrFbo = 0, hdrColor = 0, hdrNormal = 0, hdrDepth = 0;
    GLuint bloomFbo[2] = { 0, 0 }, bloomTex[2] = { 0, 0 };
} rt;

bool shadowPass = false;

GLuint makeTex(int w, int h, GLenum internalFmt, GLenum fmt, GLenum type) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, w, h, 0, fmt, type, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

void createShadowMap() {
    glGenTextures(1, &shadowTex);
    glBindTexture(GL_TEXTURE_2D, shadowTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, SHADOW_SIZE, SHADOW_SIZE, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float border[4] = { 1, 1, 1, 1 };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glGenFramebuffers(1, &shadowFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) std::cerr << "그림자 FBO 생성 실패\n";
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void createTargets(int w, int h) {
    if (rt.hdrFbo) {
        glDeleteFramebuffers(1, &rt.hdrFbo);
        glDeleteTextures(1, &rt.hdrColor);
        glDeleteTextures(1, &rt.hdrNormal);
        glDeleteRenderbuffers(1, &rt.hdrDepth);
        glDeleteFramebuffers(2, rt.bloomFbo);
        glDeleteTextures(2, rt.bloomTex);
    }
    rt.w = w;
    rt.h = h;

    rt.hdrColor = makeTex(w, h, GL_RGBA16F, GL_RGBA, GL_FLOAT);
    rt.hdrNormal = makeTex(w, h, GL_RGBA16F, GL_RGBA, GL_FLOAT);
    glGenRenderbuffers(1, &rt.hdrDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, rt.hdrDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glGenFramebuffers(1, &rt.hdrFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, rt.hdrFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt.hdrColor, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, rt.hdrNormal, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rt.hdrDepth);
    GLenum bufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, bufs);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) std::cerr << "HDR FBO 생성 실패\n";

    glGenFramebuffers(2, rt.bloomFbo);
    for (int i = 0; i < 2; ++i) {
        rt.bloomTex[i] = makeTex(std::max(1, w / 2), std::max(1, h / 2), GL_RGBA16F, GL_RGBA, GL_FLOAT);
        glBindFramebuffer(GL_FRAMEBUFFER, rt.bloomFbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt.bloomTex[i], 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void drawMesh(const Mesh& mesh, const glm::mat4& m, const Material& mat) {
    if (shadowPass) {
        glUniformMatrix4fv(shadowModelLoc, 1, GL_FALSE, glm::value_ptr(m));
    } else {
        glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(m)));
        glUniformMatrix4fv(sl.model, 1, GL_FALSE, glm::value_ptr(m));
        glUniformMatrix3fv(sl.normalMat, 1, GL_FALSE, glm::value_ptr(nm));
        glUniform3fv(sl.objColor, 1, glm::value_ptr(mat.color));
        glUniform1f(sl.Ks, mat.ks);
        glUniform1f(sl.shininess, mat.shininess);
        glUniform1f(sl.emissive, mat.emissive);
        glUniform1i(sl.pattern, mat.pattern);
        glUniform1i(sl.texLayer, mat.tex);
        glUniform1f(sl.texScale, mat.texScale);
        glUniform3f(sl.objScale, glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2])));
    }
    glBindVertexArray(mesh.vao);
    glDrawArrays(GL_TRIANGLES, 0, mesh.count);
    ++statDrawCalls;
    statTriangles += mesh.count / 3;
}

glm::mat4 TRS(glm::vec3 t, glm::vec3 s) {
    return glm::scale(glm::translate(glm::mat4(1.0f), t), s);
}

// 방향 벡터를 +Z 축으로 하는 회전 행렬
glm::mat4 orientTo(glm::vec3 dir) {
    glm::vec3 z = glm::normalize(dir);
    glm::vec3 up = fabsf(z.y) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    glm::vec3 x = glm::normalize(glm::cross(up, z));
    glm::vec3 y = glm::cross(z, x);
    return glm::mat4(glm::vec4(x, 0), glm::vec4(y, 0), glm::vec4(z, 0), glm::vec4(0, 0, 0, 1));
}

void drawEnemy(const Enemy& e) {
    EnemyRig r = enemyRig(e);
    Material skin = M_ENEMY;
    if (e.spawnT < 1.0f) skin.emissive = (1.0f - e.spawnT) * 2.0f;
    for (int i = 0; i < 2; ++i)
        drawMesh(cubeMesh, r.legs[i] * TRS({ 0, -0.45f, 0 }, { 0.2f, 0.9f, 0.22f }), M_ENEMY_DARK);
    drawMesh(cubeMesh, r.torso * glm::scale(glm::mat4(1.0f), glm::vec3(0.55f, 0.75f, 0.3f)), skin);
    drawMesh(cubeMesh, r.head * glm::scale(glm::mat4(1.0f), glm::vec3(0.3f, 0.34f, 0.3f)), skin);
    drawMesh(cubeMesh, r.head * TRS({ 0, 0.02f, 0.155f }, { 0.24f, 0.06f, 0.02f }), M_VISOR);
    for (int i = 0; i < 2; ++i)
        drawMesh(cubeMesh, r.arms[i] * TRS({ 0, -0.35f, 0 }, { 0.14f, 0.7f, 0.14f }), skin);
    drawMesh(cubeMesh, r.arms[1] * TRS({ 0, -0.75f, 0.04f }, { 0.08f, 0.34f, 0.12f }), M_DARK);
}

void drawPart(const glm::mat4& m, const Material& mat) {
    if (shadowPass && mat.emissive > 0.0f) return;   // 빛나는 부품은 그림자를 만들지 않는다
    drawMesh(cubeMesh, m, mat);
}

// 객차: 선로 좌표계(trackFrame)에 방 회전(roomToWorld)을 곱한 행렬 하나로 몸체와 부품을 모두 배치
void drawTrains() {
    Material body{ glm::vec3(0.62f, 0.64f, 0.68f), 0.9f, 72.0f, 0.0f, 0, TEX_METAL, 7.0f };
    Material dark{ glm::vec3(0.12f, 0.13f, 0.15f), 0.5f, 32.0f, 0.0f, 0 };
    Material win{ glm::vec3(1.0f, 0.8f, 0.52f), 0.0f, 1.0f, 1.1f, 0 };
    Material red{ glm::vec3(1.0f, 0.12f, 0.08f), 0.0f, 1.0f, 6.0f, 0 };
    for (int k = 0; k < (int)tracks.size(); ++k) {
        const Track& t = tracks[k];
        for (int i = 0; i < t.cars; ++i) {
            if (!carVisible(t, carS(t, i))) continue;
            glm::mat4 C = carWorld(k, i);
            drawPart(C * glm::scale(glm::mat4(1.0f), glm::vec3(CAR_LEN, CAR_H, CAR_W)), body);
            drawPart(C * TRS({ 0, -CAR_H * 0.5f + 0.3f, 0 }, { CAR_LEN * 0.99f, 0.6f, CAR_W + 0.03f }), dark);
            drawPart(C * TRS({ 0, CAR_H * 0.5f + 0.06f, 0 }, { CAR_LEN * 0.95f, 0.12f, CAR_W * 0.7f }), dark);
            drawPart(C * TRS({ 0, 0.35f, 0 }, { CAR_LEN * 0.9f, 0.75f, CAR_W + 0.04f }), win);
            // 양옆 출입문 두 개씩 + 선두·후미 행선판
            for (int d = -1; d <= 1; d += 2)
                drawPart(C * TRS({ d * CAR_LEN * 0.25f, -0.15f, 0 }, { 1.2f, 2.3f, CAR_W + 0.06f }), dark);
            if (i == 0) drawPart(C * TRS({ CAR_LEN * 0.5f + 0.02f, 0.9f, 0 }, { 0.06f, 0.4f, 1.6f }), win);
            for (int s = -1; s <= 1; s += 2) {
                if (i == 0) drawPart(C * TRS({ CAR_LEN * 0.5f + 0.03f, -0.55f, s * 0.8f }, { 0.06f, 0.32f, 0.42f }), red);
                if (i == t.cars - 1) drawPart(C * TRS({ -CAR_LEN * 0.5f - 0.03f, -0.55f, s * 0.8f }, { 0.06f, 0.32f, 0.42f }), red);
            }
        }
    }
}

// 포털: 각 면 중앙. 그 방향에 칸이 있을 때만 생기고, 방을 깨면 열린다(청록), 아니면 잠김(빨강)
void drawPortals(bool films) {
    if (!portalsOn()) return;
    bool open = roomCleared[currentCell];
    for (const auto& D : FACE_DIRS) {
        if (portalTarget(D) < 0) continue;
        int n = fabsf(D.x) > 0.5f ? 0 : (fabsf(D.y) > 0.5f ? 1 : 2);
        int a = (n + 1) % 3, b = (n + 2) % 3;
        glm::vec3 ad = glm::abs(D), c = ROOM_C + D * ROOM_HALF;
        if (!films) {
            Material fm{ open ? glm::vec3(0.3f, 0.95f, 1.0f) : glm::vec3(0.9f, 0.2f, 0.15f), 0.0f, 1.0f, open ? 3.0f : 1.2f, 0 };
            for (int pass = 0; pass < 2; ++pass) {
                int along = pass == 0 ? a : b, across = pass == 0 ? b : a;
                for (int s = -1; s <= 1; s += 2) {
                    glm::vec3 p = c - D * 0.15f, size = ad * 0.3f;
                    p[across] += s * 4.2f;
                    size[along] = 8.9f;
                    size[across] = 0.5f;
                    drawPart(TRS(p, size), fm);
                }
            }
        } else if (open) {
            Material film{ glm::vec3(0.3f, 0.9f, 1.0f), 0.0f, 1.0f, 1.2f + 0.4f * sinf(globalTime * 3.0f), 0 };
            drawMesh(cubeMesh, TRS(c - D * 0.1f, (glm::vec3(1.0f) - ad) * 8.0f + ad * 0.05f), film);
        }
    }
}

void drawWorld(bool withEffects) {
    for (const auto& b : boxes) {
        if (shadowPass && b.mat.emissive > 0.0f) continue;
        if (b.glassy) continue;     // 반투명은 drawTransparent 에서
        if (b.shellDir != glm::vec3(0.0f)) {
            // 외벽은 그림자를 드리우지 않고, 맨 위에 온 면은 열어서 하늘이 보이게 한다 (허브는 유리 지붕)
            if (shadowPass || (b.shellDir.y > 0.5f && currentCell != HUB_CELL)) continue;
            if (rotAnimActive && glm::dot(b.shellDir, preTopDir) > 0.5f && currentCell != HUB_CELL) continue;
        }
        drawMesh(cubeMesh, TRS(b.center, b.size), b.mat);
    }
    glm::mat4 W = roomToWorld();
    for (const auto& d : localDecor) drawPart(W * d.model, d.mat);
    drawTrains();
    if (!shadowPass) drawPortals(false);
    if (withEffects && grap.active) {
        glm::vec3 eye = eyePosition(), fwd = forwardDir();
        glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
        glm::vec3 hand = eye + right * 0.25f - glm::vec3(0, 0.35f, 0) + fwd * 0.4f;
        glm::vec3 d = grap.anchor - hand;
        float L = glm::length(d);
        Material rope{ glm::vec3(0.75f, 0.95f, 1.0f), 0.0f, 1.0f, 2.5f, 0 };
        if (L > 0.05f) drawMesh(cubeMesh, glm::translate(glm::mat4(1.0f), hand + d * 0.5f) * orientTo(d) * glm::scale(glm::mat4(1.0f), glm::vec3(0.035f, 0.035f, L)), rope);
    }
    for (const auto& e : enemies) drawEnemy(e);
    for (const auto& s : shards) {
        float fade = std::min(1.0f, s.life);
        glm::mat4 m = glm::translate(glm::mat4(1.0f), s.pos) * glm::rotate(glm::mat4(1.0f), s.angle, s.axis);
        Material mat = s.glow ? M_VISOR : M_ENEMY;
        mat.color = s.color;
        drawMesh(tetraMesh, m * glm::scale(glm::mat4(1.0f), glm::vec3(s.size * fade)), mat);
    }
    if (!withEffects) return;

    for (const auto& b : bullets) {
        glm::vec3 dir = glm::normalize(b.vel);
        glm::mat4 o = orientTo(dir);
        Material core{ b.fromPlayer ? glm::vec3(0.7f, 0.95f, 1.0f) : glm::vec3(1.0f, 0.45f, 0.12f), 0.0f, 1.0f, 6.0f, 0 };
        Material trail = core;
        trail.emissive = 1.6f;
        float len = b.fromPlayer ? 0.9f : 0.45f, thick = b.fromPlayer ? 0.035f : 0.07f;
        drawMesh(cubeMesh, glm::translate(glm::mat4(1.0f), b.pos - dir * len * 0.5f) * o * glm::scale(glm::mat4(1.0f), glm::vec3(thick, thick, len)), core);
        float tl = b.fromPlayer ? 3.0f : 2.2f;
        drawMesh(cubeMesh, glm::translate(glm::mat4(1.0f), b.pos - dir * (len + tl * 0.5f)) * o * glm::scale(glm::mat4(1.0f), glm::vec3(thick * 0.45f, thick * 0.45f, tl)), trail);
    }
    for (const auto& s : sparks) {
        Material m{ s.color, 0.0f, 1.0f, 4.0f, 0 };
        drawMesh(cubeMesh, TRS(s.pos, glm::vec3(0.05f * std::min(1.0f, s.life * 3.0f))), m);
    }
}

// 조명 후보(방 조명 + 달리는 객차의 창 불빛)를 월드로 옮기고, 화면에 영향이 큰 가까운 것부터 MAX 개를 보낸다
const int MAX_LIGHTS = 24;
int statLights = 0;
void uploadLights(glm::vec3 eye) {
    std::vector<LightSrc> cand;
    cand.reserve(localLights.size() + 40);
    glm::mat4 W = roomToWorld();
    for (const auto& l : localLights) cand.push_back({ glm::vec3(W * glm::vec4(l.pos, 1.0f)), l.col, l.range });
    for (int k = 0; k < (int)tracks.size(); ++k)
        for (int i = 0; i < tracks[k].cars; ++i)
            if (carVisible(tracks[k], carS(tracks[k], i)))
                cand.push_back({ glm::vec3(carWorld(k, i)[3]), glm::vec3(1.0f, 0.72f, 0.42f) * 1.6f, 10.0f });
    int n = std::min((int)cand.size(), MAX_LIGHTS);
    std::partial_sort(cand.begin(), cand.begin() + n, cand.end(), [&](const LightSrc& a, const LightSrc& b) {
        return glm::length(a.pos - eye) - a.range < glm::length(b.pos - eye) - b.range;
    });
    std::vector<glm::vec3> pos(n), col(n);
    std::vector<float> range(n);
    for (int i = 0; i < n; ++i) { pos[i] = cand[i].pos; col[i] = cand[i].col; range[i] = cand[i].range; }
    glUniform1i(sl.numLights, n);
    if (n > 0) {
        glUniform3fv(sl.lightPos, n, glm::value_ptr(pos[0]));
        glUniform3fv(sl.lightCol, n, glm::value_ptr(col[0]));
        glUniform1fv(sl.lightRange, n, range.data());
    }
    statLights = n;
}

// 반투명: 불투명한 것을 다 그린 뒤, 깊이는 쓰지 않고 카메라에서 먼 것부터(뒤→앞) 알파 블렌딩
void drawTransparent(glm::vec3 eye) {
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    glColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);   // 외곽선용 법선 버퍼는 건드리지 않는다

    std::vector<const Box*> glassList;
    for (const auto& b : boxes) if (b.glassy) glassList.push_back(&b);
    std::sort(glassList.begin(), glassList.end(), [&](const Box* a, const Box* b) {
        return glm::length(a->center - eye) > glm::length(b->center - eye);
    });
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUniform1f(sl.alpha, 0.28f);
    for (const Box* b : glassList) drawMesh(cubeMesh, TRS(b->center, b->size), b->mat);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE);      // 포털 막은 빛이므로 더하기
    glUniform1f(sl.alpha, 0.35f + 0.1f * sinf(globalTime * 2.0f));
    drawPortals(true);

    glUniform1f(sl.alpha, 1.0f);
    glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void drawViewModel(const glm::mat4& view) {
    glm::mat4 invView = glm::inverse(view);
    float bx = player.onGround ? cosf(player.bobPhase) * 0.012f : 0.0f;
    float by = player.onGround ? fabsf(sinf(player.bobPhase)) * 0.012f : 0.0f;
    float lower = player.dead ? std::min(1.0f, player.deadT) * 0.5f : 0.0f;
    glm::mat4 gun = invView * glm::translate(glm::mat4(1.0f), glm::vec3(0.21f + bx, -0.19f - by - lower, -0.5f + player.recoil * 0.05f));
    gun = glm::rotate(gun, player.recoil * 0.3f, glm::vec3(1, 0, 0));
    gun = glm::scale(gun, glm::vec3(0.75f));

    Material body{ glm::vec3(0.1f, 0.1f, 0.12f), 0.6f, 48.0f, 0.0f, 0 };
    Material glove{ glm::vec3(0.22f, 0.22f, 0.24f), 0.1f, 8.0f, 0.0f, 0 };
    Material stripe{ glm::vec3(1.0f, 0.5f, 0.15f), 0.0f, 1.0f, 1.2f, 0 };
    drawMesh(cubeMesh, gun * TRS({ 0, 0.02f, -0.1f }, { 0.06f, 0.07f, 0.3f }), body);
    drawMesh(cubeMesh, gun * TRS({ 0.031f, 0.025f, -0.1f }, { 0.004f, 0.012f, 0.22f }), stripe);
    glm::mat4 grip = glm::rotate(glm::translate(gun, glm::vec3(0, -0.08f, 0.02f)), 0.25f, glm::vec3(1, 0, 0));
    drawMesh(cubeMesh, grip * glm::scale(glm::mat4(1.0f), glm::vec3(0.055f, 0.14f, 0.08f)), body);
    drawMesh(cubeMesh, gun * TRS({ 0, -0.1f, 0.05f }, { 0.11f, 0.09f, 0.12f }), glove);
    if (player.muzzleT > 0.0f) {
        Material flash{ glm::vec3(1.0f, 0.8f, 0.45f), 0.0f, 1.0f, 8.0f, 0 };
        glm::mat4 m = glm::rotate(glm::translate(gun, glm::vec3(0, 0.02f, -0.29f)), frand(0, PI), glm::vec3(0, 0, 1));
        drawMesh(cubeMesh, m * glm::scale(glm::mat4(1.0f), glm::vec3(0.12f, 0.02f, 0.02f)), flash);
        drawMesh(cubeMesh, m * glm::scale(glm::mat4(1.0f), glm::vec3(0.02f, 0.12f, 0.02f)), flash);
    }
}

void drawFullscreen() {
    glBindVertexArray(emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

// ===================================================================
// 큐브 화면 렌더링
// ===================================================================
GLuint cubeProg;
glm::mat4 cubeVP(1.0f);

void drawCubePart(const glm::mat4& m, glm::vec3 color, float emissive) {
    glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(m)));
    glm::mat4 mvp = cubeVP * m;
    glUniformMatrix4fv(U(cubeProg, "mvp"), 1, GL_FALSE, glm::value_ptr(mvp));
    glUniformMatrix3fv(U(cubeProg, "normalMat"), 1, GL_FALSE, glm::value_ptr(nm));
    glUniform3fv(U(cubeProg, "color"), 1, glm::value_ptr(color));
    glUniform1f(U(cubeProg, "emissive"), emissive);
    glBindVertexArray(cubeMesh.vao);
    glDrawArrays(GL_TRIANGLES, 0, cubeMesh.count);
}

// 상자의 모서리 12개를 얇은 막대로 그림 (선 굵기가 드라이버마다 달라서 도형으로 처리)
void drawBoxEdges(const glm::mat4& m, glm::vec3 c, glm::vec3 half, float thick, glm::vec3 color) {
    for (int a = 0; a < 3; ++a) {
        int b = (a + 1) % 3, d = (a + 2) % 3;
        for (int sb = -1; sb <= 1; sb += 2)
            for (int sd = -1; sd <= 1; sd += 2) {
                glm::vec3 p = c, s(thick);
                p[b] += half[b] * sb;
                p[d] += half[d] * sd;
                s[a] = half[a] * 2.0f + thick;
                drawCubePart(m * TRS(p, s), color, 1.0f);
            }
    }
}

void renderCubeView(int w, int h) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    glClearColor(0.05f, 0.06f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glUseProgram(cubeProg);
    cubeVP = cubeViewProj();
    glm::mat4 cm = cubeModel();

    auto sliceSpin = [&]() {
        glm::vec3 a(0.0f);
        a[sliceAxis] = 1.0f;
        return glm::rotate(glm::mat4(1.0f), sliceAngle, a);
    };

    for (int i = 0; i < 27; ++i) {
        const Cubie& c = cubies[i];
        glm::mat4 m = cm;
        if (sliceAxis >= 0 && c.pos[sliceAxis] == sliceLayer) m = m * sliceSpin();
        m = glm::translate(m, glm::vec3(c.pos)) * glm::mat4(c.orient);
        drawCubePart(glm::scale(m, glm::vec3(0.97f)), glm::vec3(0.05f, 0.05f, 0.06f), 0.0f);
        float glow = 0.0f;
        if (i == currentCell) glow = 0.2f + 0.2f * sinf(globalTime * 5.0f);
        else if (i == hoverCell && dragMode == DRAG_NONE) glow = 0.2f;
        else if (roomCleared[i]) glow = 0.1f;   // 깬 방은 살짝 밝게
        // 스티커 색 = 그 방의 벽 색. 윗면 스티커 색을 보면 어느 벽이 하늘 쪽인지 알 수 있다
        for (int f = 0; f < 6; ++f)
            drawCubePart(m * TRS(FACE_DIRS[f] * 0.485f, glm::vec3(0.85f) - glm::abs(FACE_DIRS[f]) * 0.82f), FACE_COLORS[f], glow);
    }

    // 강조선은 칸 뒤에 가려지지 않게 깊이 검사를 끄고 그린다
    glDisable(GL_DEPTH_TEST);
    int ax = sliceAxis >= 0 ? sliceAxis : hoverAxis;
    int layer = sliceAxis >= 0 ? sliceLayer : hoverLayer;
    if (ax >= 0) {
        glm::vec3 c(0.0f), half(1.52f);
        c[ax] = (float)layer;
        half[ax] = 0.52f;
        drawBoxEdges(sliceAxis >= 0 ? cm * sliceSpin() : cm, c, half, 0.045f, glm::vec3(1.0f, 0.85f, 0.2f));
    }
    glm::mat4 curM = cm;
    if (sliceAxis >= 0 && cubies[currentCell].pos[sliceAxis] == sliceLayer) curM = curM * sliceSpin();
    drawBoxEdges(curM, glm::vec3(cubies[currentCell].pos), glm::vec3(0.5f), 0.03f, glm::vec3(0.3f, 0.95f, 1.0f));

    // 중력 방향(큐브 공간의 -Y)은 큐브와 함께 돌며 항상 아래쪽 칸을 가리킨다
    drawCubePart(cm * TRS(glm::vec3(0, -2.05f, 0), glm::vec3(0.04f, 0.8f, 0.04f)), glm::vec3(1.0f), 0.5f);
    drawCubePart(cm * TRS(glm::vec3(0, -2.47f, 0), glm::vec3(0.16f, 0.1f, 0.16f)), glm::vec3(1.0f), 0.5f);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
}

void renderFrame(int w, int h) {
    if (w != rt.w || h != rt.h) createTargets(w, h);

    glm::vec3 eye = eyePosition(), fwd = forwardDir();
    glm::vec3 up(0, 1, 0);
    if (player.dead) {
        float roll = std::min(1.0f, player.deadT) * 0.5f;
        glm::vec3 right = glm::normalize(glm::cross(fwd, up));
        up = glm::normalize(up * cosf(roll) + right * sinf(roll));
    }
    // 월드 회전 연출: 월드는 이미 회전을 마친 상태이고, 남은 회전 A 를 시점에 곱해 되감아 보여준다
    glm::mat4 rollA(1.0f);
    if (rotAnimActive) {
        float t = glm::smoothstep(0.0f, 1.0f, glm::clamp(rotAnimT, 0.0f, 1.0f));
        glm::quat q = glm::slerp(rotAnimFrom, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), t);
        rollA = glm::translate(glm::mat4(1.0f), ROOM_C) * glm::mat4_cast(q) * glm::translate(glm::mat4(1.0f), -ROOM_C);
    }
    glm::vec3 skyEye = glm::vec3(rollA * glm::vec4(eye, 1.0f));
    glm::mat4 skyView = glm::lookAt(skyEye, skyEye + fwd, up);
    glm::mat4 view = skyView * rollA;
    float fov = 75.0f - (1.0f - timeScale) * 6.0f;
    glm::mat4 proj = glm::perspective(glm::radians(fov), (float)w / (float)h, 0.03f, FAR_PLANE);

    // 해는 큐브 공간에 고정 → 방이 돌면 빛이 들어오는 방향도 달라진다
    glm::vec3 sunW = glm::transpose(glm::mat3(rollA)) * SUN_DIR;
    // 그림자 범위도 방 크기에 비례 (방 대각선을 덮도록)
    const float sx = 42.0f * ROOM_SCALE;
    glm::mat4 lightView = glm::lookAt(ROOM_C + sunW * (90.0f * ROOM_SCALE), ROOM_C, fabsf(sunW.y) > 0.95f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0));
    glm::mat4 lightVP = glm::ortho(-sx, sx, -sx, sx, 10.0f, 170.0f * ROOM_SCALE) * lightView;

    // 1) 그림자 패스
    shadowPass = true;
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo);
    glViewport(0, 0, SHADOW_SIZE, SHADOW_SIZE);
    glEnable(GL_DEPTH_TEST);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);
    glUseProgram(shadowProg);
    glUniformMatrix4fv(shadowVPLoc, 1, GL_FALSE, glm::value_ptr(lightVP));
    drawWorld(false);
    glDisable(GL_POLYGON_OFFSET_FILL);
    shadowPass = false;

    // 2) 씬 패스 (HDR, MRT)
    glBindFramebuffer(GL_FRAMEBUFFER, rt.hdrFbo);
    glViewport(0, 0, w, h);
    float clearC[4] = { 0, 0, 0, 1 }, clearN[4] = { 0.5f, 0.5f, 1.0f, 1.0f };
    glClearBufferfv(GL_COLOR, 0, clearC);
    glClearBufferfv(GL_COLOR, 1, clearN);
    glClear(GL_DEPTH_BUFFER_BIT);

    glDisable(GL_DEPTH_TEST);
    glUseProgram(skyProg);
    glm::mat4 invVP = glm::inverse(proj * skyView);
    glUniformMatrix4fv(U(skyProg, "invViewProj"), 1, GL_FALSE, glm::value_ptr(invVP));
    glUniform3fv(U(skyProg, "camPos"), 1, glm::value_ptr(skyEye));
    glUniform3fv(U(skyProg, "sunDir"), 1, glm::value_ptr(SUN_DIR));
    glUniform3fv(U(skyProg, "zenith"), 1, glm::value_ptr(SKY_ZENITH));
    glUniform3fv(U(skyProg, "horizon"), 1, glm::value_ptr(SKY_HORIZON));
    glUniform3fv(U(skyProg, "sunColor"), 1, glm::value_ptr(SUN_COLOR));
    drawFullscreen();
    glEnable(GL_DEPTH_TEST);

    glUseProgram(sceneProg);
    glUniformMatrix4fv(sl.view, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(sl.proj, 1, GL_FALSE, glm::value_ptr(proj));
    glUniformMatrix4fv(sl.lightVP, 1, GL_FALSE, glm::value_ptr(lightVP));
    // 허브(유리 지붕 기차역)는 차갑고 어두운 회청색 조명
    bool hub = currentCell == HUB_CELL;
    glm::vec3 sunC = hub ? glm::vec3(0.75f, 0.8f, 0.9f) : SUN_COLOR;
    glm::vec3 fogC = hub ? glm::vec3(0.3f, 0.35f, 0.43f) : SKY_HORIZON;
    glUniform3fv(sl.sunDir, 1, glm::value_ptr(sunW));
    glUniform3fv(sl.sunColor, 1, glm::value_ptr(sunC));
    if (hub) glUniform3f(sl.skyAmb, 0.2f, 0.25f, 0.33f);
    else glUniform3f(sl.skyAmb, 0.32f, 0.4f, 0.58f);
    if (hub) glUniform3f(sl.groundAmb, 0.1f, 0.11f, 0.13f);
    else glUniform3f(sl.groundAmb, 0.32f, 0.24f, 0.2f);
    glUniform3fv(sl.viewPos, 1, glm::value_ptr(eye));
    glUniform3fv(sl.fogColor, 1, glm::value_ptr(fogC));
    glUniform1f(sl.fogDensity, hub ? 0.016f : 0.012f);
    glUniform1f(sl.farPlane, FAR_PLANE);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, shadowTex);
    glUniform1i(sl.shadowMap, 0);
    glm::vec3 muzzleWorld = eye + fwd * 0.8f;
    glUniform3fv(sl.pointPos, 1, glm::value_ptr(muzzleWorld));
    glUniform3f(sl.pointColor, 1.0f, 0.75f, 0.4f);
    glUniform1f(sl.pointPower, player.muzzleT > 0.0f ? 6.0f : 0.0f);

    glUniform1f(sl.alpha, 1.0f);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D_ARRAY, albedoArray);
    glUniform1i(sl.albedoTex, 1);
    glActiveTexture(GL_TEXTURE0);
    uploadLights(eye);
    drawWorld(true);
    drawTransparent(eye);

    glClear(GL_DEPTH_BUFFER_BIT);
    drawViewModel(view);

    // 3) 블룸
    glDisable(GL_DEPTH_TEST);
    int bw = std::max(1, w / 2), bh = std::max(1, h / 2);
    glViewport(0, 0, bw, bh);
    glBindFramebuffer(GL_FRAMEBUFFER, rt.bloomFbo[0]);
    glUseProgram(brightProg);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, rt.hdrColor);
    glUniform1i(U(brightProg, "src"), 0);
    drawFullscreen();

    glUseProgram(blurProg);
    glUniform1i(U(blurProg, "src"), 0);
    glUniform2f(U(blurProg, "texel"), 1.0f / bw, 1.0f / bh);
    for (int i = 0; i < 8; ++i) {
        int horiz = i % 2 == 0 ? 1 : 0;
        glBindFramebuffer(GL_FRAMEBUFFER, rt.bloomFbo[horiz]);
        glBindTexture(GL_TEXTURE_2D, rt.bloomTex[1 - horiz]);
        glUniform1i(U(blurProg, "horizontal"), horiz);
        drawFullscreen();
    }

    // 4) 합성
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    glUseProgram(compositeProg);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, rt.hdrColor);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, rt.hdrNormal);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, rt.bloomTex[0]);
    glUniform1i(U(compositeProg, "sceneTex"), 0);
    glUniform1i(U(compositeProg, "normalTex"), 1);
    glUniform1i(U(compositeProg, "bloomTex"), 2);
    glUniform2f(U(compositeProg, "texel"), 1.0f / w, 1.0f / h);
    glUniform1f(U(compositeProg, "focus"), glm::clamp((1.0f - timeScale) / 0.9f, 0.0f, 1.0f));
    glUniform1f(U(compositeProg, "hurt"), std::max(player.hurtT, player.dead ? 0.6f : 0.0f));
    glUniform1f(U(compositeProg, "time"), globalTime);
    drawFullscreen();
    glActiveTexture(GL_TEXTURE0);
}

// ===================================================================
// HUD
// ===================================================================
struct Batch2D {
    std::vector<float> tris;
    void rect(float x, float y, float w, float h, const glm::vec4& c) {
        float v[6][2] = { { x, y }, { x + w, y }, { x + w, y + h }, { x, y }, { x + w, y + h }, { x, y + h } };
        for (auto& p : v) tris.insert(tris.end(), { p[0], p[1], c.r, c.g, c.b, c.a });
    }
};

struct Glyph { char c; unsigned char rows[7]; };
const Glyph FONT[] = {
    {'0',{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}}, {'1',{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}},
    {'2',{0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}}, {'3',{0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}},
    {'4',{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}}, {'5',{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}},
    {'6',{0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}}, {'7',{0x1F,0x01,0x02,0x04,0x08,0x08,0x08}},
    {'8',{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}}, {'9',{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}},
    {'A',{0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}}, {'B',{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
    {'C',{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}}, {'D',{0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}},
    {'E',{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}}, {'F',{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}},
    {'G',{0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}}, {'H',{0x11,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'I',{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}}, {'J',{0x07,0x02,0x02,0x02,0x02,0x12,0x0C}},
    {'K',{0x11,0x12,0x14,0x18,0x14,0x12,0x11}}, {'L',{0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
    {'M',{0x11,0x1B,0x15,0x15,0x11,0x11,0x11}}, {'N',{0x11,0x11,0x19,0x15,0x13,0x11,0x11}},
    {'O',{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}}, {'P',{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}},
    {'Q',{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}}, {'R',{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
    {'S',{0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}}, {'T',{0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
    {'U',{0x11,0x11,0x11,0x11,0x11,0x11,0x0E}}, {'V',{0x11,0x11,0x11,0x11,0x11,0x0A,0x04}},
    {'W',{0x11,0x11,0x11,0x15,0x15,0x15,0x0A}}, {'X',{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}},
    {'Y',{0x11,0x11,0x11,0x0A,0x04,0x04,0x04}}, {'Z',{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}},
    {':',{0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}}, {'/',{0x00,0x01,0x02,0x04,0x08,0x10,0x00}},
    {'-',{0x00,0x00,0x00,0x1F,0x00,0x00,0x00}}, {'.',{0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}},
    {'(',{0x02,0x04,0x08,0x08,0x08,0x04,0x02}}, {')',{0x08,0x04,0x02,0x02,0x02,0x04,0x08}},
    {'[',{0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}}, {']',{0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}},
};

const unsigned char* glyphFor(char ch) {
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
    for (const auto& g : FONT)
        if (g.c == ch) return g.rows;
    return nullptr;
}

float textWidth(const std::string& s, float scale) {
    return s.empty() ? 0.0f : (s.size() * 6.0f - 1.0f) * scale;
}

void drawText(Batch2D& b, float x, float y, float s, const std::string& str, const glm::vec4& col) {
    for (int pass = 0; pass < 2; ++pass) {
        glm::vec4 c = pass == 0 ? glm::vec4(0, 0, 0, col.a * 0.5f) : col;
        float off = pass == 0 ? s * 0.5f : 0.0f;
        float cx = x;
        for (char ch : str) {
            const unsigned char* g = glyphFor(ch);
            if (g)
                for (int r = 0; r < 7; ++r)
                    for (int k = 0; k < 5; ++k)
                        if (g[r] & (0x10 >> k)) b.rect(cx + k * s + off, y + (6 - r) * s - off, s, s, c);
            cx += 6.0f * s;
        }
    }
}

void drawTextCentered(Batch2D& b, float cx, float y, float s, const std::string& str, const glm::vec4& col) {
    drawText(b, cx - textWidth(str, s) * 0.5f, y, s, str, col);
}

void flushHUD(Batch2D& b, int w, int h);

void renderHUD(int w, int h) {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float ui = h / 720.0f;
    float cx = w * 0.5f, cy = h * 0.5f;
    Batch2D b;
    glm::vec4 white(1, 1, 1, 0.9f), cyan(0.45f, 0.92f, 1.0f, 1.0f), orange(1.0f, 0.55f, 0.18f, 1.0f);

    if (!player.dead) {
        float g = (focusActive ? 9.0f : 6.0f) * ui, l = 6.0f * ui, t = 2.0f * ui;
        // 줄을 걸 수 있는 곳을 보고 있으면 조준점이 초록, 기차에 매달려 있으면 노랑
        glm::vec4 cc = grap.onTrain ? glm::vec4(1.0f, 0.85f, 0.2f, 1.0f)
                     : (grappleAimOK ? glm::vec4(0.45f, 1.0f, 0.5f, 1.0f) : (focusActive ? cyan : white));
        b.rect(cx - g - l, cy - t * 0.5f, l, t, cc);
        b.rect(cx + g, cy - t * 0.5f, l, t, cc);
        b.rect(cx - t * 0.5f, cy - g - l, t, l, cc);
        b.rect(cx - t * 0.5f, cy + g, t, l, cc);
        b.rect(cx - t * 0.5f, cy - t * 0.5f, t, t, cc);
    }

    // 집중 게이지
    float bw = 240.0f * ui, bh = 7.0f * ui, bx = cx - bw * 0.5f, by = 34.0f * ui;
    b.rect(bx - 2 * ui, by - 2 * ui, bw + 4 * ui, bh + 4 * ui, glm::vec4(0, 0, 0, 0.45f));
    b.rect(bx, by, bw * focusEnergy, bh, focusActive ? glm::vec4(1, 1, 1, 0.95f) : glm::vec4(cyan.r, cyan.g, cyan.b, 0.85f));
    drawTextCentered(b, cx, by + bh + 6 * ui, 1.6f * ui, "FOCUS [RMB]", glm::vec4(cyan.r, cyan.g, cyan.b, 0.8f));

    for (int i = 0; i < 3; ++i)
        b.rect((24 + i * 22) * ui, 28 * ui, 16 * ui, 16 * ui, i < player.hp ? orange : glm::vec4(0, 0, 0, 0.4f));

    std::string ws = currentCell == HUB_CELL ? "HUB"
                   : (roomCleared[currentCell] ? "ROOM " + std::to_string(currentCell) + " CLEAR"
                                               : "WAVE " + std::to_string(wave) + "/" + std::to_string(ROOM_WAVES));
    std::string ks = "KILLS " + std::to_string(kills);
    drawText(b, w - textWidth(ws, 2.2f * ui) - 24 * ui, h - 40 * ui, 2.2f * ui, ws, white);
    drawText(b, w - textWidth(ks, 1.8f * ui) - 24 * ui, h - 66 * ui, 1.8f * ui, ks, white);
    drawText(b, 16 * ui, h - 28 * ui, 1.4f * ui, std::to_string((int)fps) + " FPS", glm::vec4(1, 1, 1, 0.5f));

    if (bannerT > 0.0f && !player.dead && !rotAnimActive)
        drawTextCentered(b, cx, h * 0.68f, 3.4f * ui, bannerText, glm::vec4(0.6f, 1.0f, 1.0f, std::min(1.0f, bannerT)));
    else if (waveBannerT > 0.0f && !player.dead && !rotAnimActive)
        drawTextCentered(b, cx, h * 0.68f, 6.0f * ui, "WAVE " + std::to_string(wave), glm::vec4(1, 1, 1, std::min(1.0f, waveBannerT)));
    if (grap.onTrain)
        drawTextCentered(b, cx, h * 0.36f, 1.8f * ui, "HOLD SHIFT - RIDING TO THE NEXT ROOM", glm::vec4(1.0f, 0.85f, 0.2f, 1.0f));
    if (helpT > 0.0f) {
        glm::vec4 hc(1, 1, 1, std::min(1.0f, helpT) * 0.85f);
        drawTextCentered(b, cx, h * 0.26f, 1.7f * ui, "WASD MOVE   SPACE JUMP   CTRL SPRINT   HOLD SHIFT GRAPPLE", hc);
        drawTextCentered(b, cx, h * 0.26f - 20 * ui, 1.7f * ui, "LMB SHOOT   HOLD RMB TO SLOW TIME   R RESTART", hc);
    }
    if (player.dead) {
        drawTextCentered(b, cx, h * 0.55f, 6.0f * ui, "YOU WERE HIT", glm::vec4(1, 0.25f, 0.2f, 1));
        drawTextCentered(b, cx, h * 0.47f, 2.2f * ui, "PRESS R TO RETRY", white);
    }
    drawText(b, 16 * ui, h - 48 * ui, 1.4f * ui, "TAB CUBE", glm::vec4(1, 1, 1, 0.6f));
    if (rotAnimActive) drawTextCentered(b, cx, h * 0.68f, 3.0f * ui, "WORLD ROTATING", glm::vec4(1.0f, 0.85f, 0.2f, 1.0f));

    flushHUD(b, w, h);
}

void renderCubeHUD(int w, int h) {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    float ui = h / 720.0f;
    Batch2D b;
    glm::vec4 white(1, 1, 1, 0.92f), dim(1, 1, 1, 0.65f), yellow(1.0f, 0.85f, 0.2f, 1.0f), cyan(0.3f, 0.95f, 1.0f, 1.0f);

    // 큐브 공간 → 프레임버퍼 픽셀 (y 위 방향)
    auto toFb = [&](glm::vec3 p) {
        glm::vec4 c = cubeVP * cubeModel() * glm::vec4(p, 1.0f);
        return glm::vec2((c.x / c.w * 0.5f + 0.5f) * w, (c.y / c.w * 0.5f + 0.5f) * h);
    };

    drawTextCentered(b, w * 0.5f, h - 44 * ui, 2.6f * ui, "CUBE", white);
    glm::vec2 cur = toFb(glm::vec3(cubies[currentCell].pos));
    drawTextCentered(b, cur.x, cur.y - 3 * ui, 1.3f * ui, "YOU", cyan);
    glm::vec2 g = toFb(glm::vec3(0, -2.7f, 0));
    drawTextCentered(b, g.x, g.y - 6 * ui, 1.3f * ui, "GRAVITY", white);

    drawTextCentered(b, w * 0.5f, 84 * ui, 1.6f * ui, "DRAG OUTSIDE THE CUBE - ROTATE WHOLE CUBE", dim);
    drawTextCentered(b, w * 0.5f, 62 * ui, 1.6f * ui, "DRAG ON A CELL - TURN THAT SLICE (YELLOW)", yellow);
    drawTextCentered(b, w * 0.5f, 40 * ui, 1.6f * ui, "DOUBLE CLICK OR ENTER - GO TO THAT CELL", dim);
    drawTextCentered(b, w * 0.5f, 18 * ui, 1.6f * ui, "R - RESET CUBE      TAB - BACK AND APPLY ROTATION", dim);

    if (cubies[currentCell].orient != worldOrient)
        drawText(b, 20 * ui, h - 48 * ui, 1.6f * ui, "YOUR WORLD WILL ROTATE", yellow);

    flushHUD(b, w, h);
}

void flushHUD(Batch2D& b, int w, int h) {
    // 픽셀 좌표 → NDC (3x3 동차좌표, 행 우선)
    float m[9] = { 2.0f / w, 0, -1, 0, 2.0f / h, -1, 0, 0, 1 };
    glUseProgram(hudProg);
    glUniformMatrix3fv(U(hudProg, "xform"), 1, GL_TRUE, m);
    glBindVertexArray(hudVao);
    glBindBuffer(GL_ARRAY_BUFFER, hudVbo);
    glBufferData(GL_ARRAY_BUFFER, b.tris.size() * sizeof(float), b.tris.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(b.tris.size() / 6));
    glBindVertexArray(0);
    glDisable(GL_BLEND);
}

// ===================================================================
// 입력 / main
// ===================================================================
double lastMouseX = 0.0, lastMouseY = 0.0;
bool firstMouse = true;

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_Q) { glfwSetWindowShouldClose(window, true); return; }
    if (cubeView) {
        bool idle = dragMode == DRAG_NONE;
        if ((key == GLFW_KEY_TAB || key == GLFW_KEY_ESCAPE) && idle) { closeCubeView(); firstMouse = true; }
        else if (key == GLFW_KEY_ENTER && idle && hoverCell >= 0) { enterCell(hoverCell); firstMouse = true; }
        else if (key == GLFW_KEY_R && idle) initCube();
        return;
    }
    if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(window, true);
    else if (key == GLFW_KEY_R) restartRun();
    else if (key == GLFW_KEY_TAB && !rotAnimActive) openCubeView();
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (!cubeView || button != GLFW_MOUSE_BUTTON_LEFT) return;
    double mx, my;
    glfwGetCursorPos(window, &mx, &my);
    if (action == GLFW_PRESS) {
        cubeMouseDown(mx, my);
    } else if (action == GLFW_RELEASE) {
        cubeMouseUp(mx, my);
        if (!cubeView) firstMouse = true;   // 더블클릭으로 칸에 들어간 경우
    }
}

void cursor_callback(GLFWwindow* window, double x, double y) {
    if (cubeView) {
        cubeMouseMove(x, y);
        return;
    }
    if (firstMouse) { lastMouseX = x; lastMouseY = y; firstMouse = false; }
    double dx = x - lastMouseX, dy = y - lastMouseY;
    lastMouseX = x;
    lastMouseY = y;
    if (player.dead || fabs(dx) > 300.0 || fabs(dy) > 300.0) return;   // 커서 캡처 직후 튐 무시
    player.yaw += (float)dx * 0.0022f;
    player.pitch = glm::clamp(player.pitch - (float)dy * 0.0022f, -1.45f, 1.45f);
}

void saveScreenshot(int w, int h, const std::string& path);

// --shot 실행 시 실제 게임 함수를 스크립트로 불러 허브 → 기차 → 방 → 클리어 → 포털 → 회전 → 재시작을 확인
void aimAt(glm::vec3 p) {
    glm::vec3 d = glm::normalize(p - eyePosition());
    player.yaw = atan2f(d.x, -d.z);
    player.pitch = asinf(glm::clamp(d.y, -1.0f, 1.0f));
}

void shotScript(int frame, int w, int h) {
    auto shot = [&](int n) { saveScreenshot(w, h, "shot_" + std::to_string(n) + ".ppm"); };
    auto pv = [](glm::vec3 p) { return std::to_string((int)p.x) + "," + std::to_string((int)p.y) + "," + std::to_string((int)p.z); };
    static int phase = 0, t0 = 0, rideCell = -1;
    static glm::vec3 portalDir(0.0f);
    if (frame == 30) { player.pitch = 0.3f; player.yaw = 0.6f; }
    if (frame == 59) {
        // 첫 장면은 가장 가까운 객차를 바라보게 한다
        float best = 1e9f;
        glm::vec3 look(0.0f);
        for (int k = 0; k < (int)tracks.size(); ++k)
            for (int i = 0; i < tracks[k].cars; ++i) {
                float s = carS(tracks[k], i);
                if (s < tracks[k].sMin + 3.0f || s > tracks[k].sMax - 3.0f) continue;
                glm::vec3 c = glm::vec3(carWorld(k, i)[3]);
                float d = glm::length(c - eyePosition());
                if (d < best && d > 12.0f) { best = d; look = c; }
            }
        if (best < 1e9f) aimAt(look);
    }
    if (frame == 60) {
        shot(0);
        std::cout << "[test] start cell " << currentCell << " hub " << (currentCell == HUB_CELL) << " tracks " << tracks.size()
                  << " decor " << localDecor.size() << " boxes " << boxes.size() << "\n";
    }
    if (phase == 0 && frame >= 61 && frame % 10 == 1 && !grap.active) {
        // 시야가 트인 객차 중 벽까지 충분히 남은 것을 골라 조준하고 Shift
        for (int k = 0; k < (int)tracks.size() && !grap.active; ++k)
            for (int i = 0; i < tracks[k].cars; ++i) {
                float s = carS(tracks[k], i);
                if (s < tracks[k].sMin + 4.0f || s > tracks[k].sMax - 25.0f) continue;
                glm::vec3 c = glm::vec3(carWorld(k, i)[3]);
                Grapple g;
                if (!castGrapple(eyePosition(), glm::normalize(c - eyePosition()), g) || !g.onTrain) continue;
                aimAt(c);
                testShift = false;
                shiftWasDown = false;
                tryGrapple();
                testShift = true;
                std::cout << "[test] grapple at frame " << frame << " active " << grap.active << " onTrain " << grap.onTrain
                          << " track " << grap.track << " car " << grap.car << "\n";
                break;
            }
    }
    if (phase == 0 && grap.onTrain && rideCell < 0) { rideCell = 1; t0 = frame; }
    if (phase == 0 && rideCell == 1 && frame == t0 + 25) {
        aimAt(glm::vec3(carWorld(grap.track, grap.car)[3]) + glm::vec3(0, -6, 0) + glm::vec3(worldOrient * tracks[grap.track].dir) * 12.0f);
        shot(1);
        std::cout << "[test] riding, player " << pv(player.pos) << "\n";
    }
    if (phase == 0 && currentCell != HUB_CELL) {
        phase = 1; t0 = frame; testShift = false;
        std::cout << "[test] train carried player to cell " << currentCell << " pos " << pv(player.pos) << " cleared " << roomCleared[currentCell] << "\n";
    }
    if (phase == 0 && frame > 2500) { std::cout << "[test] FAIL train ride timeout\n"; glfwSetWindowShouldClose(gWindow, true); }

    if (phase == 1 && frame == t0 + 150) {
        std::cout << "[test] room after 2.5s: wave " << wave << " enemies " << enemies.size() << " onGround " << player.onGround << " y " << player.pos.y << "\n";
        aimAt(ROOM_C);
        shot(2);
        // 방 강제 클리어: 마지막 웨이브까지 진행된 것으로 하고 적을 모두 처치
        wave = ROOM_WAVES;
        for (auto& e : enemies) killEnemy(e, glm::vec3(0, 0, 1));
        enemies.clear();
    }
    if (phase == 1 && frame == t0 + 160) {
        std::cout << "[test] cleared " << roomCleared[currentCell] << " portals:";
        for (const auto& D : FACE_DIRS) std::cout << " (" << pv(D) << ")->" << portalTarget(D);
        std::cout << "\n";
        // 열린 수평 포털 하나를 골라 그 앞 공중에서 포털에 줄을 건다
        for (const auto& D : FACE_DIRS)
            if (fabsf(D.y) < 0.5f && portalTarget(D) >= 0 && portalTarget(D) != HUB_CELL) { portalDir = D; break; }
        player.pos = ROOM_C + portalDir * (ROOM_HALF - 10.0f) - glm::vec3(0, 1.5f, 0);
        player.vel = glm::vec3(0.0f);
        aimAt(ROOM_C + portalDir * ROOM_HALF);
        shiftWasDown = false;
        testShift = true;
    }
    if (phase == 1 && frame == t0 + 163) {
        std::cout << "[test] portal grapple active " << grap.active << " onTrain " << grap.onTrain << " anchor " << pv(grap.anchor)
                  << " target " << portalTarget(portalDir) << "\n";
        shot(3);
    }
    static int roomBefore = -1;
    if (phase == 1 && frame == t0 + 159) roomBefore = currentCell;
    if (phase == 1 && frame > t0 + 163 && currentCell != roomBefore) {
        phase = 2; t0 = frame; testShift = false;
        std::cout << "[test] portal carried player " << roomBefore << " -> " << currentCell << "\n";
    }
    if (phase == 1 && frame > t0 + 600) { std::cout << "[test] FAIL portal timeout, player " << pv(player.pos) << "\n"; glfwSetWindowShouldClose(gWindow, true); }

    if (phase == 2 && frame == t0 + 40) {
        shot(4);
        // 큐브 회전으로 포털 너머의 방이 바뀌는지: 지금 방은 그대로 두고 옆 칸이 있는 면을 돌린다
        glm::ivec3 cur = cubies[currentCell].pos;
        for (int ax = 0; ax < 3; ++ax) {
            // 옆 칸이 회전축 위에 있으면 돌아도 제자리라 의미가 없다
            if (cur[(ax + 1) % 3] == 0 && cur[(ax + 2) % 3] == 0) continue;
            glm::vec3 D(0.0f);
            D[ax] = cur[ax] < 1 ? 1.0f : -1.0f;
            int before = portalTarget(D);
            int layer = cur[ax] + (int)D[ax];   // 옆 칸이 속한 층. 지금 칸은 이 층에 없으므로 지금 방은 돌지 않는다
            turnSlice(ax, layer, 1);
            int after = portalTarget(D);
            std::cout << "[test] turn axis " << ax << " layer " << layer << ": portal (" << pv(D) << ") " << before << " -> " << after
                      << " current cell moved " << (cubies[currentCell].pos != cur) << "\n";
            break;
        }
    }
    if (phase == 2 && frame == t0 + 45) {
        restartRun();
        std::cout << "[test] restart -> cell " << currentCell << " cleared rooms " << std::count(std::begin(roomCleared), std::end(roomCleared), true) << "\n";
    }
    if (phase == 2 && frame == t0 + 60) {
        std::cout << "[test] done\n";
        glfwSetWindowShouldClose(gWindow, true);
    }
}

// ===================================================================
// --bench: 장면마다 카메라를 한 바퀴 돌리며 프레임 시간 측정 (수직동기 끔, 매 프레임 glFinish 로 GPU 까지 포함)
// ===================================================================
struct BenchScene {
    const char* name;
    int cell;
    glm::vec3 pos;
    float pitch;
    int extraBoxes;     // 방을 꽉 채우는 스트레스용 추가 상자 수
    bool enemies;
};
const BenchScene BENCH[] = {
    { "HUB floor center", HUB_CELL, glm::vec3(0, 0, 0), 0.25f, 0, false },
    { "HUB balcony L2", HUB_CELL, glm::vec3(0, 26.0f, 24.0f), -0.1f, 0, false },
    { "ROOM 14 + enemies", 14, glm::vec3(0, 0, 0), 0.1f, 0, true },
    { "ROOM 26 arena", ARENA_CELL, glm::vec3(0, 0, 4), 0.1f, 0, true },
    { "ROOM 24 rotated", 24, glm::vec3(0, 0, 0), 0.1f, 0, true },
    { "STRESS 14 +2000 box", 14, glm::vec3(0, 0, 0), 0.2f, 2000, true },
    { "STRESS 14 +8000 box", 14, glm::vec3(0, 0, 0), 0.2f, 8000, true },
};
const int BENCH_WARM = 40, BENCH_MEAS = 240;

void benchSetup(const BenchScene& s) {
    if (s.cell == 24) turnSlice(1, 1, 1);   // 윗층을 돌려 24번 방의 방향을 바꿔 둔다
    travelTo(s.cell, glm::vec3(0.0f));
    roomCleared[s.cell] = !s.enemies;
    if (s.enemies) { spawnWave(); spawnWave(); waveTimer = 1e9f; }
    std::mt19937 r(1234);
    std::uniform_real_distribution<float> u(-ROOM_HALF + 1.0f, ROOM_HALF - 1.0f), sz(0.5f, 2.0f);
    for (int i = 0; i < s.extraBoxes; ++i) {
        glm::vec3 c(u(r), u(r) + ROOM_HALF, u(r));
        if (glm::length(glm::vec2(c.x, c.z)) < 4.0f && c.y < 4.0f) continue;   // 카메라 자리는 비움
        boxes.push_back({ c, glm::vec3(sz(r), sz(r), sz(r)), M_CRATE, false });
    }
    player.hp = 1000000;
    player.pos = s.pos;
}

// 매 프레임 루프 맨 앞에서 호출. 끝나면 false
bool benchStep(float frameMs) {
    static int scene = -1, f = 0;
    static std::vector<float> ms;
    static long long dcSum = 0, triSum = 0, lightSum = 0;
    static std::vector<std::string> report;
    const int nScenes = (int)(sizeof(BENCH) / sizeof(BENCH[0]));
    if (scene < 0) { scene = 0; f = 0; benchSetup(BENCH[0]); }
    const BenchScene& s = BENCH[scene];
    ++f;
    if (f > BENCH_WARM) {
        ms.push_back(frameMs);
        dcSum += statDrawCalls;
        triSum += statTriangles;
        lightSum += statLights;
    }
    if (f >= BENCH_WARM + BENCH_MEAS) {
        std::vector<float> sorted = ms;
        std::sort(sorted.begin(), sorted.end());
        float avg = std::accumulate(ms.begin(), ms.end(), 0.0f) / ms.size();
        float p99 = sorted[(size_t)(sorted.size() * 0.99f)];
        int n = (int)ms.size();
        int w, h;
        glfwGetFramebufferSize(gWindow, &w, &h);
        char line[256];
        snprintf(line, sizeof(line), "%-22s %4dx%-4d avg %6.2f ms (%5.0f fps)  1%%low %6.2f ms (%5.0f fps)  draws %6lld  tris %8lld  lights %2lld  boxes %5d",
                 s.name, w, h, avg, 1000.0f / avg, p99, 1000.0f / p99, dcSum / n, triSum / n, lightSum / n, (int)boxes.size());
        std::cout << "[bench] " << line << "\n";
        report.push_back(line);
        ms.clear();
        dcSum = triSum = lightSum = 0;
        ++scene;
        f = 0;
        if (scene >= nScenes) {
            std::ofstream out("bench_result.txt");
            out << "GPU " << (const char*)glGetString(GL_RENDERER) << "\n";
            for (auto& l : report) out << l << "\n";
            return false;
        }
        initCube();
        benchSetup(BENCH[scene]);
    }
    // 카메라 고정 위치에서 한 바퀴 회전
    player.pos = s.pos;
    player.vel = glm::vec3(0.0f);
    player.onGround = true;
    player.yaw = (float)f / (BENCH_WARM + BENCH_MEAS) * 2.0f * PI;
    player.pitch = s.pitch;
    return true;
}

void saveScreenshot(int w, int h, const std::string& path) {
    std::vector<unsigned char> px((size_t)w * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << w << " " << h << "\n255\n";
    for (int y = h - 1; y >= 0; --y) out.write((const char*)&px[(size_t)y * w * 3], (std::streamsize)w * 3);
}

int main(int argc, char** argv) {
    bool shotMode = argc > 1 && std::string(argv[1]) == "--shot";
    bool benchMode = argc > 1 && std::string(argv[1]) == "--bench";
    int benchW = argc > 3 ? atoi(argv[2]) : 1280, benchH = argc > 3 ? atoi(argv[3]) : 720;
    int frameCount = 0;
    if (!glfwInit()) return -1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(benchMode ? benchW : 1280, benchMode ? benchH : 720, "Cube", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return -1;
    }
    gWindow = window;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(benchMode ? 0 : 1);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, cursor_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (glfwRawMouseMotionSupported()) glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

    sceneProg = makeProgram("scene_vertex.glsl", "scene_fragment.glsl");
    shadowProg = makeProgram("shadow_vertex.glsl", "shadow_fragment.glsl");
    skyProg = makeProgram("post_vertex.glsl", "sky_fragment.glsl");
    brightProg = makeProgram("post_vertex.glsl", "bright_fragment.glsl");
    blurProg = makeProgram("post_vertex.glsl", "blur_fragment.glsl");
    compositeProg = makeProgram("post_vertex.glsl", "composite_fragment.glsl");
    hudProg = makeProgram("hud_vertex.glsl", "hud_fragment.glsl");
    cubeProg = makeProgram("cube_vertex.glsl", "cube_fragment.glsl");
    if (!sceneProg || !shadowProg || !skyProg || !brightProg || !blurProg || !compositeProg || !hudProg || !cubeProg) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    sl.model = U(sceneProg, "model");
    sl.view = U(sceneProg, "view");
    sl.proj = U(sceneProg, "proj");
    sl.lightVP = U(sceneProg, "lightVP");
    sl.normalMat = U(sceneProg, "normalMat");
    sl.objColor = U(sceneProg, "objColor");
    sl.Ks = U(sceneProg, "Ks");
    sl.shininess = U(sceneProg, "shininess");
    sl.emissive = U(sceneProg, "emissive");
    sl.pattern = U(sceneProg, "pattern");
    sl.sunDir = U(sceneProg, "sunDir");
    sl.sunColor = U(sceneProg, "sunColor");
    sl.skyAmb = U(sceneProg, "skyAmb");
    sl.groundAmb = U(sceneProg, "groundAmb");
    sl.viewPos = U(sceneProg, "viewPos");
    sl.fogColor = U(sceneProg, "fogColor");
    sl.fogDensity = U(sceneProg, "fogDensity");
    sl.farPlane = U(sceneProg, "farPlane");
    sl.shadowMap = U(sceneProg, "shadowMap");
    sl.pointPos = U(sceneProg, "pointPos");
    sl.pointColor = U(sceneProg, "pointColor");
    sl.pointPower = U(sceneProg, "pointPower");
    sl.alpha = U(sceneProg, "alpha");
    sl.objScale = U(sceneProg, "objScale");
    sl.texLayer = U(sceneProg, "texLayer");
    sl.texScale = U(sceneProg, "texScale");
    sl.albedoTex = U(sceneProg, "albedoTex");
    sl.numLights = U(sceneProg, "numLights");
    sl.lightPos = U(sceneProg, "lightPos");
    sl.lightCol = U(sceneProg, "lightCol");
    sl.lightRange = U(sceneProg, "lightRange");
    shadowModelLoc = U(shadowProg, "model");
    shadowVPLoc = U(shadowProg, "lightVP");

    cubeMesh = buildCube();
    tetraMesh = buildTetra();
    glGenVertexArrays(1, &emptyVao);
    glGenVertexArrays(1, &hudVao);
    glGenBuffers(1, &hudVbo);
    glBindVertexArray(hudVao);
    glBindBuffer(GL_ARRAY_BUFFER, hudVbo);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    createShadowMap();
    double texT0 = glfwGetTime();
    createTextures();
    std::cout << "[info] textures " << TEX_COUNT << " x " << TEX_SIZE << "^2 generated in " << (int)((glfwGetTime() - texT0) * 1000) << " ms\n"
              << "[info] GPU " << (const char*)glGetString(GL_RENDERER) << " / GL " << (const char*)glGetString(GL_VERSION) << "\n";
    cubeViewQ = glm::angleAxis(0.5f, glm::vec3(1, 0, 0)) * glm::angleAxis(-0.6f, glm::vec3(0, 1, 0));
    restartRun();

    std::cout << "=== Cube ===\n"
              << "WASD 이동, Space 점프, Ctrl 달리기, 마우스 시점\n"
              << "Shift 누르고 있기 = 줄 걸기(웹스윙). 달리는 기차에 걸면 매달려 다음 방으로\n"
              << "좌클릭 발사, 우클릭 유지 = 집중(시간 감속)\n"
              << "방을 깨면 벽 중앙의 포털이 열린다. 큐브를 돌리면 포털 너머의 방이 바뀐다\n"
              << "R 처음부터, ESC/Q 종료\n"
              << "Tab 큐브 화면: 바깥 드래그 = 큐브 전체 회전, 칸 위 드래그 = 한 면 회전\n"
              << "               더블클릭/Enter = 그 칸으로 이동, R = 큐브 초기화, Tab = 돌아가며 회전 적용\n";

    double lastTime = glfwGetTime();
    float fpsAcc = 0.0f;
    int fpsFrames = 0;
    while (!glfwWindowShouldClose(window)) {
        double curTime = glfwGetTime();
        float dt = (float)(curTime - lastTime);
        lastTime = curTime;
        if (benchMode && !benchStep(dt * 1000.0f)) break;
        statDrawCalls = statTriangles = 0;
        fpsAcc += dt;
        ++fpsFrames;
        if (fpsAcc >= 0.5f) { fps = fpsFrames / fpsAcc; fpsAcc = 0.0f; fpsFrames = 0; }
        if (dt > 0.05f) dt = 0.05f;

        update(dt);

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        if (w > 0 && h > 0) {
            if (cubeView) {
                renderCubeView(w, h);
                renderCubeHUD(w, h);
            } else {
                renderFrame(w, h);
                renderHUD(w, h);
            }
            if (shotMode) shotScript(++frameCount, w, h);
            if (benchMode) glFinish();     // GPU 가 끝날 때까지 기다려 실제 프레임 시간을 잰다
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
