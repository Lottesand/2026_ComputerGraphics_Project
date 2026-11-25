// 캡틴 방패: 단면 곡선을 Z 축으로 돌려 만드는 회전체 메쉬 + 별 (외부 모델 없이 코드로 생성)
//   지역 +Z 가 앞면, 반지름 1. 앞면은 볼록한 돔이고 고리 경계마다 V 자 홈이 파여 있다
#include "cube.h"

static const int SEG = 96;                  // 둘레 분할
static const float DOME = 0.2f;             // 가운데가 튀어나온 높이
static const float THICK = 0.035f;          // 판 두께
static const float R_BLUE = 0.4f, R_RED1 = 0.6f, R_WHITE = 0.8f, R_FACE = 0.95f;   // 고리 바깥 반지름
static const float GROOVE = 0.009f;         // 홈 반폭
static const float STAR_R = 0.36f, STAR_LIFT = 0.008f, STAR_TOP = 0.032f;

static Mesh redMesh, whiteMesh, blueMesh, steelMesh, grooveMesh, shieldStar;

static float domeZ(float r) { return DOME * (1.0f - r * r); }

static glm::vec3 spinZ(glm::vec2 p, float a) { return glm::vec3(p.x * cosf(a), p.x * sinf(a), p.y); }

// 단면 점 (반지름, z) 들을 돌린다. 진행 방향의 왼쪽이 바깥면 (반지름이 커지면 앞면 +Z)
static void addLathe(std::vector<float>& d, const std::vector<glm::vec2>& pts) {
    size_t n = pts.size();
    std::vector<glm::vec2> nor(n, glm::vec2(0.0f));
    for (size_t i = 0; i + 1 < n; ++i) {    // 이웃 선분의 법선을 평균해 부드럽게
        glm::vec2 t = glm::normalize(pts[i + 1] - pts[i]), k(-t.y, t.x);
        nor[i] += k;
        nor[i + 1] += k;
    }
    for (size_t i = 0; i + 1 < n; ++i)
        for (int s = 0; s < SEG; ++s) {
            float a0 = 2.0f * PI * s / SEG, a1 = 2.0f * PI * (s + 1) / SEG;
            auto put = [&](size_t k, float a) { pushVert(d, spinZ(pts[k], a), glm::normalize(spinZ(nor[k], a))); };
            if (pts[i + 1].x > 1e-5f) { put(i, a0); put(i + 1, a0); put(i + 1, a1); }
            if (pts[i].x > 1e-5f) { put(i, a0); put(i + 1, a1); put(i, a1); }
        }
}

// 돔 표면의 r0 ~ r1 구간
static void addDome(std::vector<float>& d, float r0, float r1, int steps) {
    std::vector<glm::vec2> p;
    for (int i = 0; i <= steps; ++i) {
        float r = r0 + (r1 - r0) * i / steps;
        p.push_back({ r, domeZ(r) });
    }
    addLathe(d, p);
}

void buildCapShield() {
    std::vector<float> red, white, blue, steel, groove, star;
    addDome(blue, 0.0f, R_BLUE - GROOVE, 10);
    addDome(red, R_BLUE + GROOVE, R_RED1 - GROOVE, 6);
    addDome(white, R_RED1 + GROOVE, R_WHITE - GROOVE, 6);
    addDome(red, R_WHITE + GROOVE, R_FACE - GROOVE, 5);
    for (float r : { R_BLUE, R_RED1, R_WHITE, R_FACE })
        addLathe(groove, { { r - GROOVE, domeZ(r - GROOVE) }, { r, domeZ(r) - 0.014f }, { r + GROOVE, domeZ(r + GROOVE) } });

    // 둥근 테두리 → 뒷면(오목한 금속판)까지 이어지는 한 줄의 단면
    float r0 = R_FACE + GROOVE, rc = (r0 + 1.0f) * 0.5f, h = (1.0f - r0) * 0.5f;
    std::vector<glm::vec2> p;
    for (int i = 0; i <= 14; ++i) {
        float a = PI - 1.5f * PI * i / 14.0f;
        p.push_back({ rc + h * cosf(a), domeZ(r0) + h * sinf(a) - 0.002f });
    }
    for (int i = 0; i <= 14; ++i) {
        float r = r0 * (1.0f - i / 14.0f);
        p.push_back({ r, domeZ(r) - THICK });
    }
    addLathe(steel, p);

    // 가운데가 솟은 면 깎인 별: 윗면 10장 + 옆벽 10장
    glm::vec3 top(0, 0, DOME + STAR_TOP);
    auto corner = [&](int i, float lift) {
        float a = PI * 0.5f + PI * i / 5.0f, r = i % 2 == 0 ? STAR_R : STAR_R * 0.382f;
        return glm::vec3(cosf(a) * r, sinf(a) * r, domeZ(r) + lift);
    };
    for (int i = 0; i < 10; ++i) {
        pushTri(star, top, corner(i, STAR_LIFT), corner(i + 1, STAR_LIFT));
        pushQuad(star, corner(i, -0.01f), corner(i + 1, -0.01f), corner(i + 1, STAR_LIFT), corner(i, STAR_LIFT));
    }
    redMesh = uploadMesh(red);
    whiteMesh = uploadMesh(white);
    blueMesh = uploadMesh(blue);
    steelMesh = uploadMesh(steel);
    grooveMesh = uploadMesh(groove);
    shieldStar = uploadMesh(star);
}

void drawCapShield(const glm::mat4& M) {
    const int BRUSHED = 4;      // scene_fragment.glsl 의 pattern 4: 동심원으로 솔질한 금속
    Material red{ glm::vec3(0.62f, 0.035f, 0.05f), 0.8f, 64.0f, 0.0f, BRUSHED };
    Material white{ glm::vec3(0.78f, 0.79f, 0.82f), 0.8f, 64.0f, 0.0f, BRUSHED };
    Material blue{ glm::vec3(0.035f, 0.1f, 0.42f), 0.8f, 64.0f, 0.0f, BRUSHED };
    Material steel{ glm::vec3(0.5f, 0.52f, 0.56f), 0.9f, 72.0f, 0.0f, BRUSHED };
    Material groove{ glm::vec3(0.07f, 0.07f, 0.08f), 0.3f, 24.0f, 0.0f, 0 };
    Material star{ glm::vec3(0.86f, 0.87f, 0.9f), 0.9f, 80.0f, 0.0f, 0 };
    Material mount{ glm::vec3(0.4f, 0.41f, 0.44f), 0.8f, 64.0f, 0.0f, 0 };
    Material strap{ glm::vec3(0.24f, 0.13f, 0.06f), 0.15f, 12.0f, 0.0f, 0 };
    drawMesh(steelMesh, M, steel);
    drawMesh(redMesh, M, red);
    drawMesh(whiteMesh, M, white);
    drawMesh(blueMesh, M, blue);
    drawMesh(grooveMesh, M, groove);
    drawMesh(shieldStar, M, star);
    // 뒷면: 가죽 끈 두 줄과 판에 고정하는 금속 받침
    for (int s = -1; s <= 1; s += 2) {
        drawMesh(cubeMesh, M * TRS({ 0, s * 0.2f, 0.045f }, { 0.92f, 0.11f, 0.03f }), strap);
        for (int e = -1; e <= 1; e += 2)
            drawMesh(cubeMesh, M * TRS({ e * 0.43f, s * 0.2f, 0.085f }, { 0.07f, 0.14f, 0.1f }), mount);
    }
}
