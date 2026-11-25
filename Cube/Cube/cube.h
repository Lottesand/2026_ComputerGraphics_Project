// Cube 공용 헤더: 모든 .cpp 가 공유하는 타입, 상수, 전역 변수(extern), 함수 원형
#pragma once

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
#include <cstddef>

// ---------------- 타입과 상수 ----------------


const float PI = 3.1415926535f;

const float FAR_PLANE = 200.0f;

const int SHADOW_SIZE = 2048;

const float PLAYER_R = 0.35f;

const float PLAYER_H = 1.8f;

const float EYE_H = 1.62f;

const float STEP_H = 0.6f;

const float ENEMY_R = 0.38f;

const float ENEMY_H = 1.95f;


const glm::vec3 SUN_DIR = glm::normalize(glm::vec3(0.5f, 0.55f, 0.65f));

   // 표면 → 태양
const glm::vec3 SUN_COLOR(1.55f, 1.25f, 0.95f);

const glm::vec3 SKY_ZENITH(0.14f, 0.2f, 0.42f);

const glm::vec3 SKY_HORIZON(1.0f, 0.66f, 0.45f);


// ===================================================================
// 메쉬
// ===================================================================
struct Mesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLsizei count = 0;
};


// ===================================================================
// 텍스처: 이미지 파일 없이 코드로 그린 512x512 이미지 6장을 텍스처 배열 한 개에 올린다
//   RGB = 색(재질 색에 곱함), A = 반사(스펙큘러) 세기
// ===================================================================
enum TexLayer { TEX_CONCRETE = 0, TEX_TILE, TEX_GLASS, TEX_METAL, TEX_PANEL, TEX_CRATE, TEX_COUNT };

const int TEX_SIZE = 512;


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
    bool rusher;            // 근접형: 총 대신 달려와서 내려친다
    float hp;               // 기본 1. 총알·방패는 1, 방패 없는 맨손은 0.5
    glm::vec3 vel;          // 지난 프레임에 움직인 속도 (욘두 화살이 앞질러 겨눌 때)
    float atkT, atkCd;      // 내려치기 준비 남은 시간(팔을 든 상태), 다음 공격까지
};


struct Bullet {
    glm::vec3 pos, vel;
    bool fromPlayer, alive;
    float life;
    int pierce = 0;     // 플레이어 총알이 적을 더 뚫고 지나갈 수 있는 수
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
    float camRoll, camFov;      // 웹스윙 1인칭 연출: 화면 기울기(라디안), 추가 시야각(도)
};


const Material M_FLOOR{ glm::vec3(0.8f, 0.78f, 0.74f), 0.25f, 24.0f, 0.0f, 1, TEX_TILE, 6.0f };

const Material M_CONCRETE{ glm::vec3(0.88f, 0.86f, 0.82f), 0.15f, 16.0f, 0.0f, 2, TEX_CONCRETE, 4.0f };

const Material M_DARK{ glm::vec3(0.16f, 0.17f, 0.2f), 0.35f, 32.0f, 0.0f, 0 };

const Material M_CRATE{ glm::vec3(0.78f, 0.42f, 0.28f), 0.2f, 16.0f, 0.0f, 2, TEX_CRATE, -1.0f };

const Material M_NEON{ glm::vec3(0.25f, 0.9f, 1.0f), 0.0f, 1.0f, 3.5f, 0 };

const Material M_ENEMY{ glm::vec3(0.06f, 0.5f, 0.55f), 1.1f, 90.0f, 0.0f, 0 };

const Material M_ENEMY_DARK{ glm::vec3(0.03f, 0.2f, 0.24f), 0.9f, 64.0f, 0.0f, 0 };

const Material M_RUSHER{ glm::vec3(0.62f, 0.12f, 0.06f), 1.0f, 80.0f, 0.0f, 0 };

const Material M_RUSHER_VISOR{ glm::vec3(1.0f, 0.55f, 0.1f), 0.0f, 1.0f, 5.0f, 0 };

const Material M_VISOR{ glm::vec3(0.4f, 1.0f, 0.95f), 0.0f, 1.0f, 5.0f, 0 };

         // 현재 방향으로 회전된 실제 월드

// 점광원 (방 좌표계). 그릴 때 방 회전을 적용하고 카메라에서 가까운 것만 셰이더로 보낸다
struct LightSrc { glm::vec3 pos, col; float range; };


// ===================================================================
// 큐브 월드: 27칸이 각각 정육면체 방 하나
// ===================================================================
const float ROOM_SCALE = 1.3f;

                      // 데모(한 변 46) 대비 방 크기 배율
const float ROOM_HALF = 23.0f * ROOM_SCALE;

const glm::vec3 ROOM_C(0.0f, ROOM_HALF, 0.0f);

      // 방은 이 점을 중심으로 회전 (바닥은 항상 y = 0)
const glm::vec3 FACE_DIRS[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };

const glm::vec3 FACE_COLORS[6] = {
    { 0.85f, 0.14f, 0.14f }, { 1.0f, 0.48f, 0.08f }, { 0.95f, 0.95f, 0.95f },
    { 1.0f, 0.85f, 0.12f }, { 0.12f, 0.68f, 0.25f }, { 0.13f, 0.33f, 0.85f },
};

const int HUB_CELL = 13;

        // (0, 0, 0) 정중앙 칸. 어느 면을 돌려도 위치가 변하지 않는 기차역 허브
const int ARENA_CELL = 26;

      // (1, 1, 1) 꼭짓점 칸. 데모 때 만든 경기장 샘플이 이 칸의 바닥에 놓인다
const int ROOM_WAVES = 3;

const int WAVE_NEXT_AT = 4;             // 남은 적이 이 수 이하가 되면 다음 웨이브가 몰려온다 (핵 앤 슬래시식 물량)
const int ENEMY_BULLET_CAP = 6;         // 적이 많아도 동시에 날아다니는 적 총알은 이만큼까지

           // 칸(방) 번호 기준. 방이 회전해 자리를 옮겨도 클리어 기록은 방을 따라간다

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


// 회전된 상자(대각선 선로 등). 축 정렬 상자가 아니라 충돌은 없고 그리기만 한다 (방 좌표계)
struct Decor { glm::mat4 model; Material mat; };


// 선로: 방 좌표계의 직선. 기차 머리는 sMin(벽 속)에서 나와 sMax(반대쪽 벽 속)로 들어간다
struct Track {
    glm::vec3 c, dir;       // 선로 위의 한 점(높이 포함), 진행 방향(수평 단위 벡터)
    float sMin, sMax, speed, phase;
    int cars;
};

const float CAR_LEN = 7.0f, CAR_GAP = 0.6f, CAR_W = 2.6f, CAR_H = 3.0f;


// ===================================================================
// 웹스윙(그래플)과 기차 매달리기
// ===================================================================
struct Grapple {
    bool active = false, onTrain = false;
    glm::vec3 anchor{ 0.0f };   // 줄이 걸린 점 (월드)
    float len = 0.0f;           // 줄 길이. 당기면서 줄어든다
    int track = -1, car = 0;
    float side = 1.0f;          // 객차의 어느 옆면에 매달렸는지
    glm::vec3 carLocal{ 0.0f }; // 기차에 건 점 (객차 좌표계). 객차와 함께 움직인다
    float lastS = 0.0f;         // 지난 프레임 객차 위치. 한 바퀴 돌아 처음으로 가면 놓친다
};



// 적 몸체 각 부위의 월드 행렬 (그리기/총구/파편 생성 공용)
struct EnemyRig { glm::mat4 root, legs[2], arms[2], torso, head; };

const int DRAG_NONE = 0, DRAG_CUBE = 1, DRAG_PENDING = 2, DRAG_SLICE = 3, DRAG_SNAP = 4;


struct SceneLocs {
    GLint model, view, proj, lightVP, normalMat, objColor, Ks, shininess, emissive, pattern;
    GLint sunDir, sunColor, skyAmb, groundAmb, viewPos, fogColor, fogDensity, farPlane, shadowMap;
    GLint pointPos, pointColor, pointPower, alpha;
    GLint objScale, texLayer, texScale, albedoTex, numLights, lightPos, lightCol, lightRange, style;
};

struct Targets {
    int w = 0, h = 0;
    GLuint hdrFbo = 0, hdrColor = 0, hdrNormal = 0, hdrDepth = 0;
    GLuint bloomFbo[2] = { 0, 0 }, bloomTex[2] = { 0, 0 };
};


// ===================================================================
// 인스턴싱: 불투명한 상자/파편은 그리는 즉시 GPU 로 보내지 않고 모았다가, 패스 끝에 메쉬마다 한 번에 그린다.
// 상자 수천 개도 그리기 호출 몇 번으로 끝나 CPU 병목(드로우 콜)이 사라진다
// ===================================================================
struct Inst {
    glm::mat4 m;
    glm::vec4 colorEmis;    // rgb, 자체발광
    glm::vec4 mat;          // ks, shininess, 텍스처 층, 텍스처 크기
    float pattern;
};

struct Batch { const Mesh* mesh; GLuint vao = 0; std::vector<Inst> items; };


// 조명 후보(방 조명 + 달리는 객차의 창 불빛)를 월드로 옮기고, 화면에 영향이 큰 가까운 것부터 MAX 개를 보낸다
const int MAX_LIGHTS = 24;


// ===================================================================
// HUD
// ===================================================================
struct Batch2D {
    std::vector<float> tris;
    void rect(float x, float y, float w, float h, const glm::vec4& c) {
        float v[6][2] = { { x, y }, { x + w, y }, { x + w, y + h }, { x, y }, { x + w, y + h }, { x, y + h } };
        for (auto& p : v) tris.insert(tris.end(), { p[0], p[1], c.r, c.g, c.b, c.a });
    }
    void tri(glm::vec2 a, glm::vec2 b, glm::vec2 c, const glm::vec4& col) {
        for (glm::vec2 p : { a, b, c }) tris.insert(tris.end(), { p.x, p.y, col.r, col.g, col.b, col.a });
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
    {'+',{0x00,0x04,0x04,0x1F,0x04,0x04,0x00}}, {'%',{0x18,0x19,0x02,0x04,0x08,0x13,0x03}},
};


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

const int BENCH_WARM = 120, BENCH_MEAS = 240;


// 방마다 다른 렌더 스타일. 허브는 실사, 다른 방은 각자 다른 그래픽
enum RenderStyle { STYLE_REAL = 0, STYLE_MINIMAL, STYLE_TOON, STYLE_PIXEL, STYLE_NOIR, STYLE_NEON, STYLE_BLUEPRINT, STYLE_COUNT };

const float RIDE_ROPE = 5.5f;

// ===================================================================
// 증강: 방을 깰 때마다 3개 중 하나 → 이번 판 동안 영구 강화. 값은 증강 전 기본 스탯
// ===================================================================
struct Stats {
    float fireInterval = 0.22f, bulletSpeed = 75.0f;
    float moveMul = 1.0f, sprintMul = 1.0f, jump = 6.5f;
    float focusDrain = 0.22f, focusRegen = 0.12f;
    float grappleRange = 70.0f, dash = 20.0f, pump = 10.0f, swingMax = 28.0f, steer = 8.0f, releasePop = 1.0f;
    int maxHp = 3, pierce = 0, pellets = 1, clearHeal = 0, killsPerHeal = 0;
};

struct Augment {
    std::string name, desc;
    int line, tier;     // 계열 번호, 단계 (1 부터)
    bool skill;         // 스킬 강화 (아직 없음) / 스탯 강화
};   // 기차에 매달려 갈 때 줄 길이

// ---------------- 전역 변수 ----------------
extern float fadeT;                     // 방을 넘어갈 때 화면을 덮는 구멍 빛 (1 → 0)
extern glm::vec3 fadeColor;
extern float arrivalHoleT;              // 도착한 방 벽에 열린 구멍이 닫히는 정도 (1 → 0)
extern glm::vec3 arrivalHolePos, arrivalHoleNormal, arrivalHoleColor;
extern Mesh diskMesh;
extern std::mt19937 rng;
extern Mesh cubeMesh, tetraMesh;
extern GLuint albedoArray;
extern Player player;
extern std::vector<Box> localBoxes;
extern std::vector<Box> boxes;
extern std::vector<LightSrc> localLights;
extern std::vector<Enemy> enemies;
extern std::vector<Bullet> bullets;
extern std::vector<Shard> shards;
extern std::vector<Spark> sparks;
extern int wave, kills;
extern float waveTimer, waveBannerT, helpT;
extern float timeScale, focusEnergy, globalTime;
extern bool focusActive;
extern float fps;
extern GLFWwindow * gWindow;
extern bool roomCleared[27];
extern Cubie cubies[27];
extern int currentCell;
extern glm::mat3 worldOrient;
extern std::vector<Decor> localDecor;
extern std::vector<Track> tracks;
extern float trainTime;
extern Grapple grap;
extern bool grappleWasDown, testGrapple, grappleAimOK;
extern bool portalArmed;
extern float bannerT;
extern std::string bannerText;
extern bool cubeView;
extern glm::quat cubeViewQ;
extern int dragMode;
extern double dragStartX, dragStartY, dragLastX, dragLastY, lastClickTime;
extern glm::vec3 grabP;
extern int grabNormalAxis, lastClickCell;
extern glm::ivec3 grabCell;
extern int sliceAxis, sliceLayer;
extern float sliceAngle, sliceTarget;
extern glm::vec2 sliceScreenVel;
extern int hoverCell, hoverAxis, hoverLayer;
extern bool rotAnimActive;
extern float rotAnimT;
extern glm::quat rotAnimFrom;
extern glm::vec3 preTopDir;
extern GLuint sceneProg, shadowProg, skyProg, brightProg, blurProg, compositeProg, hudProg;
extern GLuint emptyVao, hudVao, hudVbo;
extern SceneLocs sl;
extern long long statDrawCalls, statTriangles;
extern GLint shadowModelLoc, shadowVPLoc;
extern GLuint shadowFbo, shadowTex;
extern Targets rt;
extern bool shadowPass;
extern Batch batches[2];
extern GLuint instVbo;
extern bool batching;
extern GLint sceneInstancedLoc, shadowInstancedLoc;
extern int statLights;
extern GLuint cubeProg;
extern glm::mat4 cubeVP;
extern double lastMouseX, lastMouseY;
extern bool firstMouse;

// ===================================================================
// 변신 아이템 (랜덤 인카운터로 얻고 C 휠로 장착, Z 로 해제)
// ===================================================================
const int ITEM_COUNT = 1;
enum ItemId { ITEM_CAPTAIN = 0 };
enum ShieldMode { SH_HELD = 0, SH_OUT, SH_WAIT, SH_BACK, SH_RICO, SH_HOME };

struct CapState {
    float meleeT = 0.0f, meleeCd = 0.0f;    // 근접 동작 남은 시간, 다음 공격까지
    int meleeSide = 0;                      // 0 오른손 방패, 1 왼손 주먹
    bool guarding = false;
    float guardT = 0.0f, guardLock = 0.0f, guardRegen = 0.0f;
    int guardHits = 0;
    int shield = SH_HELD;
    glm::vec3 sPos{ 0.0f }, sDir{ 0, 0, -1 }, sStart{ 0.0f };
    float sTravel = 0.0f, sTime = 0.0f, spin = 0.0f;
    int bounces = 0;
    bool retToStart = true;
    float eCd = 0.0f, qCd = 0.0f;
};

extern const char* ITEM_NAMES[ITEM_COUNT];
extern int invSlots[8], transformItem, encounterCell, encounterItem, wheelSel;
extern bool encounterPlaced, wheelOpen, testGuard;
extern glm::vec3 encounterPos, slideVel;
extern float encounterBannerT, slideT;
extern glm::vec2 wheelVec;
extern CapState cap;
extern const int GUARD_MAX;
extern const float E_COOLDOWN, Q_COOLDOWN;

// 욘두 화살 (R 칸 소모품)
struct Arrow {
    bool active = false;
    float t = 0.0f;                 // 남은 시간
    glm::vec3 pos{ 0.0f }, dir{ 0, 0, -1 }, curlAxis{ 0, 1, 0 };
    int kills = 0;
    int phase = 0;                  // 0 원 그리기, 1 표적으로 일직선 돌진
    float phaseT = 0.0f, dashLeft = 0.0f;
    std::vector<glm::vec3> trail;   // 꼬리 (최근 위치, 앞이 최신)
};
extern int arrowCount;
extern bool arrowPickupActive;
extern glm::vec3 arrowPickupPos;
extern Arrow arrow;
extern const int ARROW_MAX;
extern const float ARROW_TIME;

extern Stats stats;
extern std::vector<Augment> augments;
extern std::vector<int> ownedAugments;
extern bool augPicking, augPickupActive;
extern glm::vec3 augPickupPos;
extern int augOffer[3], augOfferCount;
extern const int AUG_LINE_COUNT;

// ---------------- 함수 ----------------
bool capActive();
void resetTransform();
void placeEncounter();
void startEncounter(int cell, int item);
void maybeStartEncounter();
bool nearEncounter();
void collectEncounter();
void equipItem(int item);
void debugGiveItem(int item);
void revertTransform();
void equipFirstItem();
void cKeyDown();
void cKeyUp();
void updateWheel();
void wheelMouse(double dx, double dy);
bool capGuardBlocks(glm::vec3 incoming);
void guardHit();
void capMelee();
void capKeyE();
void capKeyQ();
void updateShield(float dt);
void updateCaptain(float dt);
void onEnterRoomTransform();
void damageEnemy(Enemy& e, float dmg, glm::vec3 hitDir);
void buildCapShield();
void drawCapShield(const glm::mat4& M);
void resetItems();
void onEnterRoomItems();
void scheduleArrowDrop(int atKill);
void onEnemyKilledItems(glm::vec3 pos);
void debugGiveArrow();
void useArrow();
void updateArrow(float dt);
void initAugments();
void resetAugments();
int augLineTier(int line);
const char* augLineName(int line);
int augLineTiers(int line);
void startAugmentPick();
void placeAugmentPickup();
bool nearAugmentPickup();
void tryInteract();
void chooseAugment(int k);
void augmentCardRect(int i, int w, int h, float& x, float& y, float& cw, float& ch);
int augmentCardAt(double fbX, double fbY, int w, int h);
int styleForCell(int cell);
const char* styleName(int style);
glm::vec3 styleTint(int style);
Mesh buildDisk(int segments);
glm::vec3 trackWallNormal(const Track& t, bool exitSide);
glm::vec3 trackWallPoint(const Track& t, bool exitSide);
int trackDestination(const Track& t);
float holeOpen(const Track& t, bool exitSide);
void drawHoles();
void travelTo(int id, glm::vec3 D, bool viaTrain = false);
float frand(float a, float b);
std::string filetobuf(const char* file);
GLuint compileShader(GLenum type, const std::string& src, const char* name);
GLuint makeProgram(const char* vsFile, const char* fsFile);
GLint U(GLuint p, const char* name);
void pushVert(std::vector<float>& v, glm::vec3 p, glm::vec3 n);
void pushTri(std::vector<float>& v, glm::vec3 a, glm::vec3 b, glm::vec3 c);
void pushQuad(std::vector<float>& v, glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3);
Mesh uploadMesh(const std::vector<float>& data);
Mesh buildCube();
Mesh buildTetra();
float thash(int x, int y, int seed);
float tnoise(float x, float y, int period, int seed);
float tfbm(float u, float v, int baseFreq, int seed);
float gridDist(float u, int cells);
glm::vec4 texel(int layer, float u, float v);
void createTextures();
void addBox(glm::vec3 c, glm::vec3 s, const Material& m, bool solid = true);
void buildArena();
glm::mat3 quarterTurn(int axis, int turns);
void initCube();
int cubieAt(glm::ivec3 p);
void turnSlice(int axis, int layer, int turns);
void addFaceProps(int face, std::mt19937& r, const Theme& t);
glm::mat4 roomToWorld();
glm::mat4 trackFrame(const Track& t, float s, float yOff);
float carS(const Track& t, int i);
bool carVisible(const Track& t, float s);
void buildHub();
void clearPortalZones();
void buildLocalWorld(int cell);
void rebuildWorld();
bool segAABB(glm::vec3 p0, glm::vec3 p1, glm::vec3 bmin, glm::vec3 bmax, float& tHit);
bool segHitsCylinder(glm::vec3 p0, glm::vec3 p1, glm::vec3 base, float r, float h);
bool segmentBlocked(glm::vec3 p0, glm::vec3 p1, float* tOut = nullptr);
void pushOutCircle(glm::vec3& p, float r, const Box& b);
bool circleOverlapsBoxXZ(glm::vec3 p, float r, const Box& b);
float groundHeightAt(glm::vec3 p, float r);
bool spotIsFree(glm::vec3 p, float r);
glm::vec3 forwardDir();
glm::vec3 eyePosition();
void spawnSparks(glm::vec3 p, glm::vec3 color, int n, float speed);
void spawnWave();
void resetGame();
glm::mat4 carWorld(int k, int i);
bool castGrapple(glm::vec3 o, glm::vec3 d, Grapple& out);
void releaseGrapple();
void ropeDash();
void tryGrapple();
bool portalsOn();
int portalTarget(glm::vec3 D);
void restartRun();
void updatePortals();
void updateTrainRide(float dt);
EnemyRig enemyRig(const Enemy& e);
glm::vec3 enemyMuzzle(const Enemy& e);
void killEnemy(Enemy& e, glm::vec3 hitDir);
void hurtPlayer();
void firePlayer();
void updatePlayer(float dt);
void updateEnemies(float dt);
void updateBullets(float dt);
void updateDebris(float dt);
glm::mat4 cubeModel();
glm::mat4 cubeViewProj();
glm::vec2 cubeToWindow(glm::vec3 p);
bool pickCube(double mx, double my, glm::vec3& hit, int& normalAxis);
glm::ivec3 cellFromHit(glm::vec3 p);
void updateHover(double mx, double my);
glm::vec2 screenVelocity(glm::vec3 p, int axis);
void cubeMouseDown(double mx, double my);
void cubeMouseMove(double mx, double my);
void cubeMouseUp(double mx, double my);
void updateCubeView(float dt);
void openCubeView();
void applyCubeToWorld();
void leaveCubeInput();
void closeCubeView();
void enterCell(int id);
void update(float realDt);
GLuint makeTex(int w, int h, GLenum internalFmt, GLenum fmt, GLenum type);
void createShadowMap();
void createTargets(int w, int h);
void createInstancing();
void flushBatches();
void drawMesh(const Mesh& mesh, const glm::mat4& m, const Material& mat);
glm::mat4 TRS(glm::vec3 t, glm::vec3 s);
glm::mat4 orientTo(glm::vec3 dir);
void drawEnemy(const Enemy& e);
void drawPart(const glm::mat4& m, const Material& mat);
void drawTrains();
void drawPortals(bool films);
void drawWorld(bool withEffects);
void uploadLights(glm::vec3 eye);
void drawTransparent(glm::vec3 eye);
void drawViewModel(const glm::mat4& view);
void drawFullscreen();
void drawCubePart(const glm::mat4& m, glm::vec3 color, float emissive);
void drawBoxEdges(const glm::mat4& m, glm::vec3 c, glm::vec3 half, float thick, glm::vec3 color);
void renderCubeView(int w, int h);
void renderFrame(int w, int h);
const unsigned char* glyphFor(char ch);
float textWidth(const std::string& s, float scale);
void drawText(Batch2D& b, float x, float y, float s, const std::string& str, const glm::vec4& col);
void drawTextCentered(Batch2D& b, float cx, float y, float s, const std::string& str, const glm::vec4& col);
void renderHUD(int w, int h);
void renderCubeHUD(int w, int h);
void flushHUD(Batch2D& b, int w, int h);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
void cursor_callback(GLFWwindow* window, double x, double y);
void aimAt(glm::vec3 p);
void shotScript(int frame, int w, int h);
void benchSetup(const BenchScene& s);
bool benchStep(float frameMs);
void saveScreenshot(int w, int h, const std::string& path);
int main(int argc, char** argv);
