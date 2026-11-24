// 월드 데이터, 큐브 상태(27칸), 방 생성, 충돌
#include "cube.h"

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

Player player;

std::vector<Box> localBoxes;    // 회전 전 방의 원본
std::vector<Box> boxes;std::vector<LightSrc> localLights;
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

void addBox(glm::vec3 c, glm::vec3 s, const Material& m, bool solid) {
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
       // 이 수만큼 웨이브를 막으면 방 클리어 → 포털이 열린다
bool roomCleared[27];
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

bool segmentBlocked(glm::vec3 p0, glm::vec3 p1, float* tOut) {
    float best = 2.0f;
    for (const auto& b : boxes) {
        if (!b.solid) continue;
        float t;
        if (segAABB(p0, p1, b.bmin(), b.bmax(), t) && t < best) best = t;
    }
    if (!spaceRoom() && p1.y < 0.0f && p0.y >= 0.0f) best = std::min(best, p0.y / (p0.y - p1.y));
    // 행성: 선분과 구의 첫 교점
    glm::vec3 d = p1 - p0;
    float dd = glm::dot(d, d);
    for (const auto& pl : planets) {
        if (dd < 1e-10f) break;
        glm::vec3 m = p0 - pl.c;
        float b = glm::dot(m, d), c = glm::dot(m, m) - pl.r * pl.r, disc = b * b - dd * c;
        if (disc < 0.0f) continue;
        float t = (-b - sqrtf(disc)) / dd;
        if (c < 0.0f) t = 0.0f;     // 이미 안쪽
        if (t >= 0.0f && t < best) best = t;
    }
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
    float ground = spaceRoom() ? -1e9f : 0.0f;     // 우주 방은 바닥이 없다
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
    localPlanets.clear();
    if (cell == HUB_CELL) { buildHub(); return; }
    if (cell == SPACE_CELL) { buildSpace(); return; }
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
    planets = localPlanets;
    for (auto& p : planets) p.c = ROOM_C + worldOrient * (p.c - ROOM_C);
}
