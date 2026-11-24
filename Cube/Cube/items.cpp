// 소모품 (R 칸): 욘두 화살. 전투 중 적이 쓰러질 때 가끔 떨어지고, 주우면 R 칸에 쌓인다(최대 3).
// R 로 쓰면 5초 동안 원을 그리듯 휘어 날며 적을 차례로 꿰뚫는다 (긴 붉은 꼬리)
#include "cube.h"

int arrowCount = 0;                 // R 칸에 든 욘두 화살 수
bool arrowPickupActive = false;     // 지금 방 바닥에 떨어진 화살
glm::vec3 arrowPickupPos(0.0f);
Arrow arrow;
static int roomKills = 0, dropAtKill = -1;   // 이 방에서 몇 번째 처치 때 떨어뜨릴지 (-1 이면 이번 방은 없음)

const int ARROW_MAX = 3;
const float ARROW_TIME = 5.0f;
const float ARROW_ARC_SPEED = 45.0f, ARROW_ARC_TURN = 11.0f;   // 원 그리기: 속도, 회전(rad/s) → 반지름 약 4
const float ARROW_ARC_TIME = 2.0f * PI / ARROW_ARC_TURN;          // 원을 한 바퀴 다 그린 뒤 꽂는다 (약 0.57초)
const float ARROW_DASH_SPEED = 110.0f;                                              // 표적을 정하면 이 속도로 일직선
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
    arrow.curlAxis = glm::vec3(0, 1, 0);
    arrow.phase = 0;
    arrow.phaseT = ARROW_ARC_TIME;
}

// 가장 가까운 살아 있는 적
static int arrowTarget(glm::vec3 p) {
    float best = 1e9f;
    int idx = -1;
    for (int i = 0; i < (int)enemies.size(); ++i) {
        const Enemy& e = enemies[i];
        if (!e.alive || e.spawnT < 0.5f) continue;
        float L = glm::length(e.pos + glm::vec3(0, 1.0f, 0) - p);
        if (L < best) { best = L; idx = i; }
    }
    return idx;
}

// 진짜 욘두 화살처럼: 원을 그리며 돌다가(ARC) → 표적을 하나 정하면 그 적이 도착할 자리로 일직선 돌진(DASH)해
// 꿰뚫고 지나간 뒤 다시 원을 그린다. 돌진 중에는 방향을 바꾸지 않는다 (쫓아가며 휘지 않음)
void updateArrow(float dt) {
    updatePickup();
    if (!arrow.active) return;
    arrow.t -= dt;
    float speed = ARROW_ARC_SPEED;
    if (arrow.phase == 0) {
        arrow.dir = glm::normalize(glm::vec3(glm::rotate(glm::mat4(1.0f), ARROW_ARC_TURN * dt, arrow.curlAxis) * glm::vec4(arrow.dir, 0.0f)));
        arrow.phaseT -= dt;
        if (arrow.phaseT <= 0.0f) {
            int k = arrowTarget(arrow.pos);
            if (k >= 0) {
                // 앞질러 겨누기: 적이 지금 속도로 움직인다고 보고 화살이 닿을 때의 자리를 노린다
                const Enemy& e = enemies[k];
                glm::vec3 aim = e.pos + glm::vec3(0, 1.0f, 0);
                for (int it = 0; it < 2; ++it) aim = e.pos + glm::vec3(0, 1.0f, 0) + e.vel * (glm::length(aim - arrow.pos) / ARROW_DASH_SPEED);
                arrow.dir = glm::normalize(aim - arrow.pos);
                arrow.dashLeft = glm::length(aim - arrow.pos) + 2.5f;   // 조금 지나쳐서 꽂힌 뒤 빠져나온다
                arrow.phase = 1;
            } else {
                arrow.phaseT = ARROW_ARC_TIME;      // 적이 없으면 계속 맴돈다
            }
        }
    } else {
        speed = ARROW_DASH_SPEED;
        arrow.dashLeft -= speed * dt;
        if (arrow.dashLeft <= 0.0f) {
            // 다시 원을 그린다. 도는 면은 매번 조금씩 다르게
            arrow.phase = 0;
            arrow.phaseT = ARROW_ARC_TIME;
            glm::vec3 r(frand(-1, 1), frand(-1, 1) + 1.5f, frand(-1, 1));
            glm::vec3 ax = glm::cross(arrow.dir, r);
            if (glm::length(ax) > 1e-3f) arrow.curlAxis = glm::normalize(ax);
        }
    }
    glm::vec3 p0 = arrow.pos, p1 = p0 + arrow.dir * speed * dt;
    // 방 밖으로 못 나간다 (벽 앞에서 안쪽으로 튼다)
    glm::vec3 rel = p1 - ROOM_C;
    for (int i = 0; i < 3; ++i)
        if (fabsf(rel[i]) > ROOM_HALF - 1.0f) {
            rel[i] = glm::clamp(rel[i], -(ROOM_HALF - 1.0f), ROOM_HALF - 1.0f);
            arrow.dir[i] = -arrow.dir[i];
        }
    p1 = ROOM_C + rel;
    if (p1.y < 0.5f) { p1.y = 0.5f; arrow.dir.y = fabsf(arrow.dir.y); }
    for (auto& e : enemies)
        if (e.alive && e.spawnT >= 0.5f && segHitsCylinder(p0, p1, e.pos, ENEMY_R + 0.35f, ENEMY_H)) {
            spawnSparks(e.pos + glm::vec3(0, 1.2f, 0), glm::vec3(1.0f, 0.2f, 0.1f), 10, 4.0f);
            damageEnemy(e, 1.0f, arrow.dir);
            ++arrow.kills;
        }
    arrow.pos = p1;
    // 꼬리: 최근 위치를 길게 남긴다
    arrow.trail.insert(arrow.trail.begin(), p1);
    if (arrow.trail.size() > 90) arrow.trail.pop_back();
    if (arrow.t <= 0.0f) arrow = Arrow{};
}
