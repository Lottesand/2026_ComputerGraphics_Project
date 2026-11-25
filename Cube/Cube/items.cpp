// 소모품 (R 칸): 욘두 화살. 전투 중 적이 쓰러질 때 가끔 떨어지고, 주우면 R 칸에 쌓인다(최대 3).
// R 로 쓰면 5초 동안 휘어 날며 적을 차례로 꿰뚫고, 일렬로 선 적은 직선으로 한 번에 관통한다 (긴 붉은 꼬리)
#include "cube.h"

int arrowCount = 0;                 // R 칸에 든 욘두 화살 수
bool arrowPickupActive = false;     // 지금 방 바닥에 떨어진 화살
glm::vec3 arrowPickupPos(0.0f);
Arrow arrow;
static int roomKills = 0, dropAtKill = -1;   // 이 방에서 몇 번째 처치 때 떨어뜨릴지 (-1 이면 이번 방은 없음)

const int ARROW_MAX = 3;
const float ARROW_TIME = 5.0f;
const float ARROW_CURVE_SPEED = 300.0f;     // 휘어 날 때 속도
const float ARROW_DASH_SPEED = 1000.0f;     // 일직선 관통 속도
const float ARROW_LOOP_R = 5.0f;            // 뒤쪽 적에게 돌아설 때 회전 반지름 (300 / 5 = 초당 60 rad)
const float ARROW_MAX_CURV = 1.0f;          // 가장 급하게 꺾는 정도 (1 / 반지름)
const float ARROW_CURVE_HIT = 0.5f, ARROW_DASH_HIT = 0.9f;   // 적 반지름에 더하는 판정 폭 (직선은 "비슷하게 일렬"도 꿰도록 넓게)
const float ARROW_LINE_SNAP = 0.6f;         // 2명 이상이 이 각도(rad) 안에서 일렬이면 휘지 않고 바로 직선으로
const float ARROW_ROW_SNAP = 1.6f;          // 3명 이상 일렬이면 크게 꺾어서라도 직선으로
const float ARROW_ONE_SNAP = 0.1f, ARROW_ONE_DIST = 14.0f;   // 1명이면 거의 정면이고 멀 때만 직선으로 (가까우면 휘며 꿴다)
const float ARROW_CURVE_LIMIT = 0.3f;       // 이만큼 휘어도 못 맞히면 직선으로 꽂는다
const float ARROW_DROP_CHANCE = 0.55f;      // 방 한 번 싸울 때 떨어질 확률 (한두 번 싸우면 한 번은 보게)

void resetItems() {
    arrowCount = 0;
    arrowPickupActive = false;
    arrow = Arrow{};
    roomKills = 0;
    dropAtKill = -1;
}

// 방에 들어올 때: 아직 못 깬 방이면 확률로 "몇 번째 처치 때 떨어질지"를 정해 둔다
void onEnterRoomItems() {
    arrowPickupActive = false;
    arrow = Arrow{};
    roomKills = 0;
    scheduleArrowDrop((!roomCleared[currentCell] && currentCell != HUB_CELL && frand(0, 1) < ARROW_DROP_CHANCE) ? (int)frand(4.0f, 20.999f) : -1);
}

void scheduleArrowDrop(int atKill) { roomKills = 0; dropAtKill = atKill; }

void onEnemyKilledItems(glm::vec3 pos) {
    if (++roomKills != dropAtKill || arrowPickupActive) return;
    dropAtKill = -1;
    arrowPickupActive = true;
    arrowPickupPos = glm::vec3(pos.x, groundHeightAt(pos, 0.3f), pos.z);
}

void debugGiveArrow() {
    arrowCount = std::min(ARROW_MAX, arrowCount + 1);
    bannerText = "DEBUG - YONDU ARROW +1   [R] USE";
    bannerT = 2.0f;
}

// 화살 줍기: 닿으면 자동으로 (칸이 꽉 차 있으면 그대로 둔다)
static void updatePickup() {
    if (!arrowPickupActive || player.dead || arrowCount >= ARROW_MAX) return;
    if (glm::length(player.pos + glm::vec3(0, 0.9f, 0) - (arrowPickupPos + glm::vec3(0, 0.9f, 0))) > 1.8f) return;
    arrowPickupActive = false;
    ++arrowCount;
    bannerText = "YONDU ARROW   [R] USE";
    bannerT = 2.0f;
}

void useArrow() {
    if (arrowCount <= 0 || arrow.active || player.dead) return;
    --arrowCount;
    arrow = Arrow{};
    arrow.active = true;
    arrow.t = ARROW_TIME;
    arrow.dir = forwardDir();
    arrow.pos = eyePosition() + arrow.dir * 0.8f;
}

static bool arrowCanHit(const Enemy& e) { return e.alive && e.spawnT >= 0.5f; }
static glm::vec3 arrowAim(const Enemy& e) { return e.pos + glm::vec3(0, 1.0f, 0); }
static float angleBetween(glm::vec3 a, glm::vec3 b) { return acosf(glm::clamp(glm::dot(a, b), -1.0f, 1.0f)); }

// p 에서 d 쪽 직선에 꿰이는 적 수. reach 에 가장 먼 적까지의 거리
static int arrowLine(glm::vec3 p, glm::vec3 d, float& reach) {
    int n = 0;
    reach = 0.0f;
    for (const auto& e : enemies)
        if (arrowCanHit(e) && segHitsCylinder(p, p + d * (ROOM_HALF * 3.5f), e.pos, ENEMY_R + ARROW_DASH_HIT, ENEMY_H)) {
            ++n;
            reach = std::max(reach, glm::dot(arrowAim(e) - p, d));
        }
    return n;
}

static void arrowDash(glm::vec3 d) {
    float reach;
    int n = arrowLine(arrow.pos, d, reach);
    if (n == 0) return;
    arrow.dir = d;
    arrow.dashLeft = reach + 2.5f;      // 조금 지나쳐 빠져나온다
    arrow.phase = 1;
    ++arrow.dashes;
    arrow.maxLine = std::max(arrow.maxLine, n);
}

// 줄의 끝에 선 적 찾기: 그 적에서 다른 적 쪽으로 그은 직선에 3명 이상 꿰이면 그 줄부터 노린다
static void arrowFindRow() {
    arrow.rowN = 1;
    for (const auto& e : enemies) {
        if (!arrowCanHit(e)) continue;
        for (const auto& o : enemies) {
            if (&o == &e || !arrowCanHit(o)) continue;
            glm::vec3 d = arrowAim(o) - arrowAim(e);
            if (glm::length(d) < 0.3f) continue;
            float r;
            int n = arrowLine(arrowAim(e), glm::normalize(d), r);
            if (n >= 3 && n > arrow.rowN) { arrow.rowN = n; arrow.rowPos = arrowAim(e); }
        }
    }
}

// 다음 움직임 정하기. 일렬이면 직선 관통, 아니면 휘어서 노릴 자리를 goal 에. 적이 없으면 false
static bool arrowPlan(glm::vec3& goal) {
    float r, best = 1e9f, lineBest = -1.0f;
    glm::vec3 lineDir(0.0f);
    bool found = false;
    if (arrow.curveT == 0.0f) arrowFindRow();      // 하나 꿰거나 돌진을 마칠 때마다 한 번
    for (const auto& e : enemies) {
        if (!arrowCanHit(e)) continue;
        glm::vec3 to = arrowAim(e) + e.vel * (glm::length(arrowAim(e) - arrow.pos) / ARROW_CURVE_SPEED) - arrow.pos;
        float L = glm::length(to);
        if (L < 0.3f) { goal = arrow.pos + arrow.dir; return true; }   // 겹쳐 있으면 그대로 나아가며 꿴다
        glm::vec3 d = to / L;
        float ang = angleBetween(arrow.dir, d);
        int n = arrowLine(arrow.pos, d, r);
        if ((n >= 3 && ang < ARROW_ROW_SNAP) || (n >= 2 && ang < ARROW_LINE_SNAP) || (ang < ARROW_ONE_SNAP && L > ARROW_ONE_DIST)) {
            float score = (float)n - ang;
            if (score > lineBest) { lineBest = score; lineDir = d; }
        }
        // 가깝고 덜 꺾어도 되고, 뒤로 줄지어 선 적이 많은 쪽부터
        float cost = (L + 8.0f) * (1.0f + ang) / (float)std::max(n, 1);
        if (arrow.rowN >= 3 && glm::length(arrowAim(e) - arrow.rowPos) < 1.5f) cost /= (float)arrow.rowN;
        if (cost < best) { best = cost; goal = arrow.pos + to; found = true; }
    }
    if (!found) return false;
    if (lineBest > -1.0f) arrowDash(lineDir);
    else if (arrow.curveT > ARROW_CURVE_LIMIT) arrowDash(glm::normalize(goal - arrow.pos));
    return true;
}

// 목표를 정확히 지나는 원호를 따라 h 초만큼 꺾는다 (현의 길이 L, 접선과의 각 a → 곡률 = 2 sin a / L)
static void arrowSteer(glm::vec3 goal, float h) {
    glm::vec3 to = goal - arrow.pos;
    float L = std::max(glm::length(to), 0.3f);
    glm::vec3 d = to / L, ax = glm::cross(arrow.dir, d);
    float a = angleBetween(arrow.dir, d);
    if (glm::length(ax) < 1e-3f) {
        if (a < 1.0f) return;
        ax = glm::cross(arrow.dir, glm::vec3(0.01f, 1.0f, 0.02f));     // 정반대 쪽이면 수평으로 돌아선다
        if (glm::length(ax) < 1e-3f) ax = glm::vec3(1, 0, 0);
        ax = glm::cross(ax, arrow.dir);
    }
    float curv = 2.0f * sinf(a) / L;
    if (a > PI * 0.5f) curv = std::max(curv, 1.0f / ARROW_LOOP_R);
    curv = std::min(curv, ARROW_MAX_CURV);
    float turn = std::min(curv * ARROW_CURVE_SPEED * h, a);
    arrow.dir = glm::normalize(glm::vec3(glm::rotate(glm::mat4(1.0f), turn, glm::normalize(ax)) * glm::vec4(arrow.dir, 0.0f)));
}

// 쉬지 않고 이어 난다: 흩어진 적은 원호를 그리며 차례로 꿰고(phase 0), 휘는 도중이라도 일렬로 꿸 각이 나오면
// 그 순간 직선으로 관통한다(phase 1, 방향 고정). 빨라서 한 프레임을 짧은 구간으로 나눠 움직인다
void updateArrow(float dt) {
    updatePickup();
    if (!arrow.active) return;
    arrow.t -= dt;
    for (float left = dt; left > 1e-6f;) {
        glm::vec3 goal(0.0f);
        bool hunting = arrow.phase == 1 || arrowPlan(goal);
        arrow.idle = !hunting;
        if (arrow.idle) {       // 적이 없으면 그 자리에 멈춰 떤다(떨림은 그릴 때만). 꼬리는 빠르게 거둔다
            arrow.curveT = 0.0f;
            size_t cut = std::min(arrow.trail.size(), (size_t)6);
            arrow.trail.resize(arrow.trail.size() - cut);
            break;
        }
        bool dash = arrow.phase == 1;
        float speed = dash ? ARROW_DASH_SPEED : ARROW_CURVE_SPEED;
        float h = std::min(left, (dash ? 2.0f : 0.8f) / speed);
        left -= h;
        if (dash) {
            arrow.dashLeft -= speed * h;
        } else {
            arrow.curveT += h;
            arrowSteer(goal, h);
        }
        glm::vec3 p0 = arrow.pos, p1 = p0 + arrow.dir * speed * h;
        // 방 밖으로 못 나간다 (벽 앞에서 안쪽으로 튼다)
        glm::vec3 rel = p1 - ROOM_C;
        bool bounced = false;
        for (int i = 0; i < 3; ++i) {
            float lim = ROOM_HALF - (i == 1 ? 0.5f : 1.0f);
            if (fabsf(rel[i]) > lim) {
                rel[i] = glm::clamp(rel[i], -lim, lim);
                arrow.dir[i] = rel[i] > 0.0f ? -fabsf(arrow.dir[i]) : fabsf(arrow.dir[i]);
                bounced = true;
            }
        }
        p1 = ROOM_C + rel;
        for (auto& e : enemies)
            if (arrowCanHit(e) && segHitsCylinder(p0, p1, e.pos, ENEMY_R + (dash ? ARROW_DASH_HIT : ARROW_CURVE_HIT), ENEMY_H)) {
                spawnSparks(e.pos + glm::vec3(0, 1.2f, 0), glm::vec3(1.0f, 0.2f, 0.1f), 10, 4.0f);
                damageEnemy(e, 1.0f, arrow.dir);
                ++arrow.kills;
                if (!dash) ++arrow.curveKills;
                arrow.curveT = 0.0f;
            }
        arrow.pos = p1;
        if (dash && (bounced || arrow.dashLeft <= 0.0f)) { arrow.phase = 0; arrow.curveT = 0.0f; }
        arrow.trail.insert(arrow.trail.begin(), p1);    // 꼬리: 최근 위치를 길게 남긴다
        if (arrow.trail.size() > 160) arrow.trail.pop_back();
    }
    if (arrow.t <= 0.0f) arrow = Arrow{};
}
