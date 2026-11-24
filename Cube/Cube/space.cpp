// 우주 정거장 방 (SPACE_CELL): 벽 없는 우주, 무중력, 행성 근처에서만 생기는 중력, 큰 정거장, 공중에 뜬 원형 포털
#include "cube.h"

std::vector<Planet> localPlanets, planets;    // 방 좌표계 원본 / 지금 방 방향으로 돌린 실제 위치

bool spaceRoom() { return currentCell == SPACE_CELL; }

// 행성 쪽으로 당기는 중력. 표면에서는 보통 방과 같은 세기(18), 거리에 반비례하고 반지름의 3.4배 밖에서는 0.
// shortcut: 실제(거리 제곱)보다 완만하게 줄여 조금 떨어진 곳에서도 확실히 끌려온다. 더 현실적으로 하려면 제곱으로
glm::vec3 gravityAt(glm::vec3 p) {
    if (!spaceRoom()) return glm::vec3(0, -18.0f, 0);
    glm::vec3 g(0.0f);
    for (const auto& pl : planets) {
        glm::vec3 d = pl.c - p;
        float L = glm::length(d);
        if (L < 1e-3f) continue;
        float s = 18.0f * pl.r / std::max(L, pl.r) * (1.0f - glm::smoothstep(pl.r * 2.2f, pl.r * 3.4f, L));
        g += d / L * s;
    }
    return g;
}

// 구(center, radius)를 행성 밖으로 밀어내고 행성 쪽으로 들어가는 속도를 없앤다. 닿았으면 표면 법선을 돌려준다
bool collidePlanets(glm::vec3& center, glm::vec3& vel, float radius, glm::vec3* normalOut) {
    bool hit = false;
    for (const auto& pl : planets) {
        glm::vec3 d = center - pl.c;
        float L = glm::length(d), minL = pl.r + radius;
        if (L >= minL || L < 1e-4f) continue;
        glm::vec3 n = d / L;
        center = pl.c + n * minL;
        float vn = glm::dot(vel, n);
        if (vn < 0.0f) vel -= n * vn;
        if (normalOut) *normalOut = n;
        hit = true;
    }
    return hit;
}

bool insidePlanet(glm::vec3 p, float margin) {
    for (const auto& pl : planets)
        if (glm::length(p - pl.c) < pl.r + margin) return true;
    return false;
}

// 열린 포털 고리에서 접선 방향으로 튀는 불꽃 (원을 그리며 도는 마법 포털 느낌)
void emitPortalSparks(float dt) {
    if (!spaceRoom() || !roomCleared[currentCell]) return;
    static float acc = 0.0f;
    acc += dt * 90.0f;
    int n = (int)acc;
    acc -= n;
    for (const auto& D : FACE_DIRS) {
        if (portalTarget(D) < 0) continue;
        glm::vec3 c = ROOM_C + D * ROOM_HALF;
        glm::vec3 u = glm::normalize(glm::cross(D, fabsf(D.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0)));
        glm::vec3 v = glm::cross(D, u);
        for (int i = 0; i < n; ++i) {
            float a = frand(0.0f, 2.0f * PI);
            glm::vec3 radial = u * cosf(a) + v * sinf(a), tangent = -u * sinf(a) + v * cosf(a);
            glm::vec3 p = c + radial * (4.2f + frand(-0.15f, 0.15f));
            glm::vec3 col = glm::mix(glm::vec3(1.0f, 0.45f, 0.08f), glm::vec3(1.0f, 0.85f, 0.4f), frand(0, 1));
            sparks.push_back({ p, tangent * frand(5.0f, 9.0f) + radial * frand(0.5f, 2.0f) + D * frand(-0.5f, 0.5f), col, frand(0.15f, 0.4f) });
        }
    }
}

// 방 좌표계 위치(방 중심 기준)
static glm::vec3 R(float x, float y, float z) { return ROOM_C + glm::vec3(x, y, z); }

static void ringDecor(glm::vec3 c, glm::mat4 tilt, float radius, int segs, glm::vec3 seg, const Material& m) {
    for (int i = 0; i < segs; ++i) {
        float a = 2.0f * PI * i / segs;
        glm::mat4 M = glm::translate(glm::mat4(1.0f), c) * tilt * glm::rotate(glm::mat4(1.0f), a, glm::vec3(0, 1, 0)) *
                      glm::translate(glm::mat4(1.0f), glm::vec3(radius, 0, 0)) * glm::scale(glm::mat4(1.0f), seg);
        localDecor.push_back({ M, m });
    }
}

void buildSpace() {
    localPlanets.clear();
    // 행성: 포털 자리(각 면 중앙)와 도착 지점(면 중앙 안쪽 선)을 피해 대각선 쪽에 둔다. 가운데 큰 행성이 스윙의 중심
    Material terran{ glm::vec3(0.1f, 0.28f, 0.68f), 0.5f, 40.0f, 0.0f, 5 };
    Material rocky{ glm::vec3(0.72f, 0.36f, 0.22f), 0.1f, 12.0f, 0.0f, 6 };
    Material gas{ glm::vec3(0.86f, 0.66f, 0.42f), 0.25f, 20.0f, 0.0f, 4 };
    Material moon{ glm::vec3(0.62f, 0.62f, 0.64f), 0.05f, 8.0f, 0.0f, 6 };
    Material ice{ glm::vec3(0.75f, 0.88f, 1.0f), 0.9f, 64.0f, 0.0f, 6 };
    Material lava{ glm::vec3(0.95f, 0.38f, 0.1f), 0.1f, 12.0f, 0.35f, 6 };
    localPlanets.push_back({ R(0, 0, 0), 7.0f, terran });
    localPlanets.push_back({ R(14, -15, 14), 5.0f, rocky });
    localPlanets.push_back({ R(15, 12, -16), 6.5f, gas });
    localPlanets.push_back({ R(-14, -13, 13), 3.5f, moon });
    localPlanets.push_back({ R(12, 15, 13), 3.0f, ice });
    localPlanets.push_back({ R(-15, -6, -17), 3.5f, lava });
    // 가스 행성의 고리 (그리기 전용)
    glm::mat4 tilt = glm::rotate(glm::mat4(1.0f), 0.35f, glm::vec3(1, 0, 0.3f));
    Material ringA{ glm::vec3(0.8f, 0.72f, 0.58f), 0.1f, 8.0f, 0.0f, 0 }, ringB{ glm::vec3(0.6f, 0.5f, 0.42f), 0.1f, 8.0f, 0.0f, 0 };
    ringDecor(R(15, 12, -16), tilt, 9.5f, 72, glm::vec3(0.9f, 0.05f, 1.7f), ringA);
    ringDecor(R(15, 12, -16), tilt, 11.5f, 84, glm::vec3(0.95f, 0.05f, 1.6f), ringB);

    // 우주 정거장: 중력 아래 기준 왼쪽 위 (방 좌표 -x, +y). 일부는 원래 방 크기 밖, 넓어진 범위 안에 걸친다
    glm::vec3 S = R(-27, 22, -8);
    Material hull{ glm::vec3(0.86f, 0.87f, 0.9f), 0.6f, 48.0f, 0.0f, 0, TEX_PANEL, 6.0f };
    Material metal{ glm::vec3(0.5f, 0.52f, 0.56f), 0.9f, 72.0f, 0.0f, 0, TEX_METAL, 5.0f };
    Material dark{ glm::vec3(0.14f, 0.15f, 0.18f), 0.5f, 32.0f, 0.0f, 0 };
    Material solar{ glm::vec3(0.12f, 0.2f, 0.48f), 1.2f, 90.0f, 0.0f, 3 };
    Material window{ glm::vec3(1.0f, 0.85f, 0.55f), 0.0f, 1.0f, 2.2f, 0 };
    Material beacon{ glm::vec3(1.0f, 0.15f, 0.1f), 0.0f, 1.0f, 6.0f, 0 };
    Material cyan{ glm::vec3(0.3f, 0.9f, 1.0f), 0.0f, 1.0f, 3.0f, 0 };
    addBox(S, glm::vec3(9.0f), hull);                                       // 코어
    addBox(S, glm::vec3(1.6f, 1.6f, 44.0f), metal);                         // 주 트러스 (z 축)
    for (float z = -20.0f; z <= 20.0f; z += 4.0f)
        if (fabsf(z) > 5.0f) addBox(S + glm::vec3(0, 0, z), glm::vec3(3.4f, 0.35f, 0.35f), dark, false);
    for (float zz : { -17.0f, 15.0f })                                      // 태양광 날개 4장
        for (int sx = -1; sx <= 1; sx += 2) {
            addBox(S + glm::vec3(sx * 2.5f, 0, zz), glm::vec3(3.4f, 0.4f, 0.4f), dark);
            addBox(S + glm::vec3(sx * 10.5f, 0, zz), glm::vec3(13.0f, 0.25f, 7.0f), solar);
        }
    addBox(S + glm::vec3(9.5f, 0, 0), glm::vec3(10.0f, 4.5f, 4.5f), hull); // 실험 모듈 (방 안쪽 방향)
    addBox(S + glm::vec3(-9.0f, 0, 0), glm::vec3(9.0f, 4.5f, 4.5f), hull);  // 거주 모듈
    addBox(S + glm::vec3(0, 8.0f, 0), glm::vec3(4.5f, 7.0f, 4.5f), hull);   // 관측 탑
    addBox(S + glm::vec3(0, 12.0f, 0), glm::vec3(10.0f, 0.6f, 10.0f), metal);   // 탑 위 착륙장
    addBox(S + glm::vec3(0, -7.0f, 0), glm::vec3(3.2f, 5.0f, 3.2f), metal); // 도킹 포트
    // 창문 띠, 착륙장 테두리 등, 끝 표시등 (빛나는 부품은 충돌 없음)
    for (int s = -1; s <= 1; s += 2) {
        addBox(S + glm::vec3(9.5f, 0.6f, s * 2.27f), glm::vec3(8.5f, 0.5f, 0.06f), window, false);
        addBox(S + glm::vec3(-9.0f, 0.6f, s * 2.27f), glm::vec3(7.5f, 0.5f, 0.06f), window, false);
        addBox(S + glm::vec3(s * 4.53f, 1.5f, 0), glm::vec3(0.06f, 0.5f, 6.0f), window, false);
        addBox(S + glm::vec3(0, 12.32f, s * 4.9f), glm::vec3(9.6f, 0.06f, 0.15f), cyan, false);
        addBox(S + glm::vec3(s * 4.9f, 12.32f, 0), glm::vec3(0.15f, 0.06f, 9.6f), cyan, false);
        addBox(S + glm::vec3(0, 0, s * 22.2f), glm::vec3(0.5f), beacon, false);
    }
    // 회전 거주 고리 (코어 둘레, z 축) + 바퀴살 4개. 기울어진 상자라 그리기 전용
    glm::mat4 toZ = glm::rotate(glm::mat4(1.0f), PI * 0.5f, glm::vec3(1, 0, 0));
    ringDecor(S, toZ, 14.0f, 28, glm::vec3(1.8f, 2.2f, 3.4f), hull);
    ringDecor(S + glm::vec3(0, 0, 1.75f), toZ, 14.0f, 28, glm::vec3(1.2f, 0.4f, 0.12f), window);
    for (int k = 0; k < 4; ++k) {
        float a = PI * 0.25f + k * PI * 0.5f;
        glm::mat4 M = glm::translate(glm::mat4(1.0f), S) * glm::rotate(glm::mat4(1.0f), a, glm::vec3(0, 0, 1)) *
                      glm::translate(glm::mat4(1.0f), glm::vec3(9.0f, 0, 0)) * glm::scale(glm::mat4(1.0f), glm::vec3(9.5f, 0.8f, 0.8f));
        localDecor.push_back({ M, metal });
    }
    for (glm::vec3 o : { glm::vec3(0, 0, 6), glm::vec3(0, 0, -6), glm::vec3(10, 3.5f, 0), glm::vec3(-9, 3.5f, 0), glm::vec3(0, 13.5f, 0) })
        localLights.push_back({ S + o, glm::vec3(1.0f, 0.88f, 0.7f) * 2.4f, 16.0f });
    for (int s = -1; s <= 1; s += 2) localLights.push_back({ S + glm::vec3(0, 0, s * 22.8f), glm::vec3(1.0f, 0.2f, 0.1f) * 2.0f, 9.0f });
}
