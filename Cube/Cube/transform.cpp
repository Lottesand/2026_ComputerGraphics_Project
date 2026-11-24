// 변신 아이템: 랜덤 인카운터, 인벤토리 휠(C), 변신 해제(Z), 캡틴 아메리카(방패 근접·가드 반사·던지기·튕기기, 달리기 슬라이드)
#include "cube.h"

const char* ITEM_NAMES[ITEM_COUNT] = { "CAPTAIN AMERICA" };
int invSlots[8];                    // 휠 칸 (0 = 맨 위, 시계방향). -1 이면 빈 칸
int transformItem = -1;             // 지금 변신한 아이템 (-1 = 기본 플레이어)
static bool itemObtained[ITEM_COUNT];
int encounterCell = -1, encounterItem = -1;
bool encounterPlaced = false;       // 인카운터 칸에 들어와 있어서 아이템이 바닥에 놓였는지
glm::vec3 encounterPos(0.0f);
float encounterBannerT = 0.0f;
bool wheelOpen = false;
int wheelSel = -1;
glm::vec2 wheelVec(0.0f);
static double cDownTime = -1.0;
CapState cap;
float slideT = 0.0f;
glm::vec3 slideVel(0.0f);
bool testGuard = false;

const int GUARD_MAX = 6;            // 이만큼 막으면 가드가 깨진다
const float GUARD_TIME = 3.0f, E_COOLDOWN = 1.0f, Q_COOLDOWN = 8.0f;
const float SHIELD_SPEED = 42.0f, THROW_RANGE = 48.0f;
const int RICO_BOUNCES = 12;

bool capActive() { return transformItem == ITEM_CAPTAIN && !player.dead; }

void resetTransform() {
    for (int& s : invSlots) s = -1;
    transformItem = -1;
    for (bool& o : itemObtained) o = false;
    encounterCell = encounterItem = -1;
    encounterPlaced = false;
    encounterBannerT = 0.0f;
    wheelOpen = false;
    cDownTime = -1.0;
    cap = CapState{};
    slideT = 0.0f;
}

// ---------------- 랜덤 인카운터 ----------------
void placeEncounter() {
    encounterPlaced = encounterCell >= 0 && currentCell == encounterCell;
    if (!encounterPlaced) return;
    encounterPos = glm::vec3(10.0f, 0.0f, 10.0f);
    for (int k = 0; k < 120; ++k) {
        glm::vec3 p(frand(-ROOM_HALF + 4.0f, ROOM_HALF - 4.0f), 0.0f, frand(-ROOM_HALF + 4.0f, ROOM_HALF - 4.0f));
        // 바닥 가운데(아래층 포털)와 증강 물체 자리는 피한다
        if (glm::length(glm::vec2(p.x, p.z)) > 8.0f && spotIsFree(p, 1.2f) && glm::length(p - augPickupPos) > 4.0f) { encounterPos = p; break; }
    }
    encounterPos.y = groundHeightAt(encounterPos, 0.6f);
}

void startEncounter(int cell, int item) {
    encounterCell = cell;
    encounterItem = item;
    encounterBannerT = 6.0f;
    placeEncounter();
}

// 방을 깰 때마다 절반 확률로, 아직 안 얻은 아이템이 26칸 중 한 곳(지금 방 제외)에 나타난다
void maybeStartEncounter() {
    if (encounterCell >= 0) return;
    int item = -1;
    for (int i = 0; i < ITEM_COUNT && item < 0; ++i) if (!itemObtained[i]) item = i;
    if (item < 0 || frand(0, 1) > 0.5f) return;
    int cell;
    do { cell = (int)frand(0.0f, 26.999f); } while (cell == HUB_CELL || cell == currentCell);
    startEncounter(cell, item);
}

bool nearEncounter() {
    return encounterPlaced && !player.dead && glm::length(player.pos + glm::vec3(0, 0.9f, 0) - (encounterPos + glm::vec3(0, 1.4f, 0))) < 3.2f;
}

void collectEncounter() {
    for (int& s : invSlots)
        if (s < 0) { s = encounterItem; break; }
    itemObtained[encounterItem] = true;
    bannerText = std::string("OBTAINED - ") + ITEM_NAMES[encounterItem] + "   [C] TRANSFORM";
    bannerT = 4.0f;
    encounterCell = encounterItem = -1;
    encounterPlaced = false;
}

// 디버그 (Shift + 1): 아이템을 바로 인벤토리에 넣는다. 이미 있으면 그대로, 그 아이템 인카운터가 떠 있으면 지운다
void debugGiveItem(int item) {
    for (int s : invSlots) if (s == item) return;
    for (int& s : invSlots)
        if (s < 0) { s = item; break; }
    itemObtained[item] = true;
    if (encounterItem == item) { encounterCell = encounterItem = -1; encounterPlaced = false; }
    bannerText = std::string("DEBUG - ") + ITEM_NAMES[item] + " ADDED   [C] TRANSFORM";
    bannerT = 2.5f;
}

// ---------------- 변신과 인벤토리 휠 ----------------
void equipItem(int item) {
    if (item < 0 || item == transformItem) return;
    transformItem = item;
    cap = CapState{};
    releaseGrapple();
    bannerText = std::string("TRANSFORM - ") + ITEM_NAMES[item];
    bannerT = 2.5f;
    helpT = 8.0f;
}

void revertTransform() {
    if (transformItem < 0) return;
    transformItem = -1;
    cap = CapState{};
    slideT = 0.0f;
    bannerText = "BACK TO NORMAL";
    bannerT = 1.5f;
}

void equipFirstItem() {
    for (int s : invSlots)
        if (s >= 0) { equipItem(s); return; }
}

void cKeyDown() { cDownTime = glfwGetTime(); }

// 짧게 누르면 시계방향으로 맨 앞 아이템, 꾹 눌러 휠을 열었으면 마우스로 가리킨 칸
void cKeyUp() {
    if (wheelOpen) {
        if (wheelSel >= 0) equipItem(invSlots[wheelSel]);
        wheelOpen = false;
    } else if (cDownTime >= 0.0) {
        equipFirstItem();
    }
    cDownTime = -1.0;
}

void updateWheel() {
    if (cDownTime >= 0.0 && !wheelOpen && glfwGetTime() - cDownTime > 0.2) {
        wheelOpen = true;
        wheelVec = glm::vec2(0.0f);
        wheelSel = -1;
    }
}

// 휠이 열려 있는 동안 마우스 이동을 모아 방향으로 칸을 고른다 (맨 위 0, 시계방향)
void wheelMouse(double dx, double dy) {
    wheelVec += glm::vec2((float)dx, (float)-dy);
    float L = glm::length(wheelVec);
    if (L > 120.0f) wheelVec *= 120.0f / L;
    if (L < 30.0f) { wheelSel = -1; return; }
    float a = atan2f(wheelVec.x, wheelVec.y);
    if (a < 0.0f) a += 2.0f * PI;
    wheelSel = (int)((a + PI / 8.0f) / (PI / 4.0f)) % 8;
}

// ---------------- 캡틴 아메리카 ----------------
static glm::vec3 flatForward() { return glm::vec3(sinf(player.yaw), 0.0f, -cosf(player.yaw)); }

// incoming: 공격이 날아오는 방향. 가드 중이고 정면 쪽에서 오면 막는다
bool capGuardBlocks(glm::vec3 incoming) {
    return capActive() && cap.guarding && glm::dot(-glm::normalize(incoming), forwardDir()) > 0.3f;
}

void guardHit() {
    cap.guardHits++;
    cap.guardRegen = 0.0f;
    if (cap.guardHits >= GUARD_MAX) {
        cap.guarding = false;
        cap.guardLock = 3.0f;
        cap.guardHits = 0;
        bannerText = "GUARD BROKEN";
        bannerT = 1.5f;
    }
}

// 좌클릭: 방패를 들고 있으면 오른손 방패 ↔ 왼손 주먹 번갈아, 방패가 없으면 약한 맨손만
void capMelee() {
    if (!capActive() || cap.meleeCd > 0.0f || cap.guarding) return;
    bool held = cap.shield == SH_HELD;
    cap.meleeSide = held ? 1 - cap.meleeSide : 1;
    cap.meleeT = 0.22f;
    cap.meleeCd = held ? 0.3f : 0.38f;
    float range = held ? 2.9f : 2.2f, dmg = held ? 1.0f : 0.5f;
    glm::vec3 fH = flatForward();
    for (auto& e : enemies) {
        if (!e.alive || e.spawnT < 0.5f) continue;
        glm::vec3 to = e.pos - player.pos;
        float dy = to.y;
        to.y = 0.0f;
        float d = glm::length(to);
        if (d > range + ENEMY_R || fabsf(dy) > 2.0f) continue;
        if (d > 0.3f && glm::dot(to / d, fH) < 0.35f) continue;
        spawnSparks(e.pos + glm::vec3(0, 1.2f, 0), held ? glm::vec3(0.9f, 0.9f, 1.0f) : glm::vec3(1.0f, 0.7f, 0.4f), 6, 3.0f);
        damageEnemy(e, dmg, fH);
        if (e.alive) e.pos += fH * 0.8f;    // 살아남으면 밀려난다
    }
}

// 선분이 처음 닿는 벽/구조물과 그 면의 법선 (바닥 평면 포함)
static bool castWall(glm::vec3 p0, glm::vec3 p1, float& tHit, glm::vec3& nHit) {
    float best = 2.0f;
    for (const auto& b : boxes) {
        if (!b.solid) continue;
        float t;
        if (!segAABB(p0, p1, b.bmin(), b.bmax(), t) || t >= best) continue;
        best = t;
        glm::vec3 hp = p0 + (p1 - p0) * t, mn = b.bmin(), mx = b.bmax();
        float dmin = 1e9f;
        for (int a = 0; a < 3; ++a) {
            if (hp[a] - mn[a] < dmin) { dmin = hp[a] - mn[a]; nHit = glm::vec3(0.0f); nHit[a] = -1.0f; }
            if (mx[a] - hp[a] < dmin) { dmin = mx[a] - hp[a]; nHit = glm::vec3(0.0f); nHit[a] = 1.0f; }
        }
    }
    if (p1.y < 0.0f && p0.y >= 0.0f) {
        float t = p0.y / (p0.y - p1.y);
        if (t < best) { best = t; nHit = glm::vec3(0, 1, 0); }
    }
    tHit = best;
    return best <= 1.0f;
}

// 방패가 지나가는 선분에 걸린 적은 모두 피해 (관통)
static int shieldSweep(glm::vec3 p0, glm::vec3 p1) {
    int n = 0;
    glm::vec3 d = p1 - p0;
    if (glm::length(d) < 1e-5f) return 0;
    for (auto& e : enemies)
        if (e.alive && e.spawnT >= 0.5f && segHitsCylinder(p0, p1, e.pos, ENEMY_R + 0.4f, ENEMY_H)) {
            damageEnemy(e, 1.0f, glm::normalize(d));
            ++n;
        }
    return n;
}

// 방패가 다음에 쫓을 적: 지금 자리에서 보이는 가장 가까운 적
static bool shieldTarget(glm::vec3 p, glm::vec3& dir) {
    float best = 45.0f;
    bool found = false;
    for (const auto& e : enemies) {
        if (!e.alive || e.spawnT < 0.5f) continue;
        glm::vec3 c = e.pos + glm::vec3(0, 1.0f, 0);
        float L = glm::length(c - p);
        if (L < 0.5f || L >= best || segmentBlocked(p, c)) continue;
        best = L;
        dir = (c - p) / L;
        found = true;
    }
    return found;
}

// E: 방패를 던진다 / 이미 나가 있으면 날아온 길을 거슬러 돌아오게
void capKeyE() {
    if (!capActive()) return;
    if (cap.shield == SH_HELD) {
        if (cap.eCd > 0.0f) return;
        glm::vec3 fwd = forwardDir();
        cap.shield = SH_OUT;
        cap.sPos = cap.sStart = eyePosition() + fwd * 0.9f;
        cap.sDir = fwd;
        cap.sTravel = 0.0f;
        cap.guarding = false;
    } else if (cap.shield == SH_OUT || cap.shield == SH_WAIT) {
        cap.shield = SH_BACK;
        cap.retToStart = true;
    }
}

// Q: 지형과 적 사이를 직선으로 튕겨 다닌다. 방향은 부딪힐 때만 바뀌고, 그때 보이는 적을 향한다
void capKeyQ() {
    if (!capActive() || cap.shield != SH_HELD || cap.qCd > 0.0f) return;
    glm::vec3 fwd = forwardDir();
    cap.shield = SH_RICO;
    cap.sPos = eyePosition() + fwd * 0.9f;
    cap.sDir = fwd;
    glm::vec3 d;
    if (shieldTarget(cap.sPos, d)) cap.sDir = d;
    cap.bounces = 0;
    cap.sTime = 0.0f;
    cap.guarding = false;
}

void updateShield(float dt) {
    if (transformItem != ITEM_CAPTAIN || cap.shield == SH_HELD) return;
    cap.spin += dt * 20.0f;
    cap.sTime += dt;
    float step = SHIELD_SPEED * dt;
    glm::vec3 eye = eyePosition();
    switch (cap.shield) {
    case SH_OUT: {
        glm::vec3 p1 = cap.sPos + cap.sDir * step;
        float t;
        glm::vec3 n;
        if (castWall(cap.sPos, p1, t, n)) {
            p1 = cap.sPos + (p1 - cap.sPos) * t - cap.sDir * 0.3f;   // 벽에 꽂혀 기다린다
            cap.shield = SH_WAIT;
        }
        shieldSweep(cap.sPos, p1);
        cap.sTravel += glm::length(p1 - cap.sPos);
        cap.sPos = p1;
        if (cap.sTravel >= THROW_RANGE) cap.shield = SH_WAIT;
        break;
    }
    case SH_BACK: {
        // 던진 자리까지 같은 직선을 거슬러 온 뒤 플레이어 손으로
        glm::vec3 target = cap.retToStart ? cap.sStart : eye;
        glm::vec3 d = target - cap.sPos;
        float L = glm::length(d);
        glm::vec3 p1 = L <= step ? target : cap.sPos + d / L * step;
        shieldSweep(cap.sPos, p1);
        cap.sPos = p1;
        if (cap.retToStart && L <= step) cap.retToStart = false;
        if (!cap.retToStart && glm::length(eye - cap.sPos) < 1.2f) { cap.shield = SH_HELD; cap.eCd = E_COOLDOWN; }
        break;
    }
    case SH_RICO: {
        glm::vec3 p1 = cap.sPos + cap.sDir * step;
        // 이번 걸음에 걸린 가장 가까운 적 → 맞히고 그 자리에서 다음 적으로 방향을 튼다
        Enemy* hitE = nullptr;
        float hitAlong = 1e9f;
        for (auto& e : enemies)
            if (e.alive && e.spawnT >= 0.5f && segHitsCylinder(cap.sPos, p1, e.pos, ENEMY_R + 0.4f, ENEMY_H)) {
                float along = glm::dot(e.pos + glm::vec3(0, 1.0f, 0) - cap.sPos, cap.sDir);
                if (along < hitAlong) { hitAlong = along; hitE = &e; }
            }
        float t;
        glm::vec3 n;
        bool wall = castWall(cap.sPos, p1, t, n);
        if (hitE && (!wall || hitAlong <= t * step)) {
            cap.sPos += cap.sDir * glm::clamp(hitAlong, 0.0f, step);
            damageEnemy(*hitE, 1.0f, cap.sDir);
            ++cap.bounces;
            glm::vec3 d;
            if (shieldTarget(cap.sPos, d)) cap.sDir = d;
            else cap.shield = SH_HOME;
        } else if (wall) {
            cap.sPos = cap.sPos + (p1 - cap.sPos) * t + n * 0.15f;
            cap.sDir = glm::reflect(cap.sDir, n);
            ++cap.bounces;
            glm::vec3 d;
            if (shieldTarget(cap.sPos, d)) cap.sDir = d;
        } else {
            cap.sPos = p1;
        }
        if (cap.bounces >= RICO_BOUNCES || cap.sTime > 7.0f) cap.shield = SH_HOME;
        break;
    }
    case SH_HOME: {
        glm::vec3 d = eye - cap.sPos;
        float L = glm::length(d);
        if (L < 1.2f) { cap.shield = SH_HELD; cap.qCd = Q_COOLDOWN; break; }
        glm::vec3 p1 = cap.sPos + d / L * std::min(step, L);
        shieldSweep(cap.sPos, p1);
        cap.sPos = p1;
        break;
    }
    default: break;
    }
}

// 매 프레임 (updatePlayer 끝에서): 타이머, 가드, 좌클릭 근접
void updateCaptain(float dt) {
    cap.meleeT = std::max(0.0f, cap.meleeT - dt);
    cap.meleeCd -= dt;
    cap.eCd = std::max(0.0f, cap.eCd - dt);
    cap.qCd = std::max(0.0f, cap.qCd - dt);
    cap.guardLock -= dt;
    bool rmb = glfwGetMouseButton(gWindow, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS || testGuard;
    bool canGuard = cap.shield == SH_HELD && cap.guardLock <= 0.0f && cap.meleeT <= 0.0f;
    if (rmb && canGuard) {
        cap.guarding = true;
        cap.guardT += dt;
        if (cap.guardT > GUARD_TIME) { cap.guarding = false; cap.guardLock = 1.0f; cap.guardT = 0.0f; }   // 계속 들고 있을 수는 없다
    } else {
        cap.guarding = false;
        cap.guardT = 0.0f;
        cap.guardRegen += dt;
        if (cap.guardRegen > 1.5f && cap.guardHits > 0) { cap.guardHits--; cap.guardRegen = 0.0f; }
    }
    if (glfwGetMouseButton(gWindow, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) capMelee();
}

// 방을 옮기면 나가 있던 방패는 손으로 돌아오고, 인카운터 아이템은 그 방이면 놓인다
void onEnterRoomTransform() {
    if (cap.shield != SH_HELD) cap.shield = SH_HELD;
    cap.guarding = false;
    slideT = 0.0f;
    placeEncounter();
}
