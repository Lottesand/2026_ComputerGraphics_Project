// 게임 로직: 플레이어, 적, 총알, 웹스윙, 포털, 방 이동
#include "cube.h"

// ===================================================================
// 게임 로직
// ===================================================================
glm::vec3 forwardDir() {
    return glm::vec3(sinf(player.yaw) * cosf(player.pitch), sinf(player.pitch), -cosf(player.yaw) * cosf(player.pitch));
}

glm::vec3 eyePosition() {
    float bob = player.onGround ? sinf(player.bobPhase * 2.0f) * 0.035f : 0.0f;
    glm::vec3 e = player.pos + glm::vec3(0, EYE_H - player.landDip + bob - (slideT > 0.0f ? 0.55f : 0.0f), 0);   // 슬라이드 중엔 눈이 낮다
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
    // 12, 18, 24 에 증강을 하나 먹을 때마다 6% 씩 더 (26개면 약 2.5배)
    int count = (int)std::lround((6 + 6 * wave) * (1.0f + 0.06f * (float)ownedAugments.size()));
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
        e.shootCd = frand(2.5f, 5.0f);
        e.spawnT = -frand(0.0f, 1.2f);     // 한꺼번에 솟지 않고 조금씩 엇갈려 올라온다
        e.strafe = frand(0, 1) < 0.5f ? -1.0f : 1.0f;
        e.alive = true;
        e.rusher = i % 2 == 1;     // 절반은 달려드는 근접형
        e.hp = 1.0f;
        enemies.push_back(e);
    }
}

void resetGame() {
    enemies.clear(); bullets.clear(); shards.clear(); sparks.clear();
    player = Player{};
    player.pos = glm::vec3(0, 0, 4);
    for (int attempt = 0; attempt < 60 && !spotIsFree(player.pos, 0.6f); ++attempt)
        player.pos = glm::vec3(frand(-18, 18), 0, frand(-18, 18));
    player.hp = stats.maxHp;
    player.onGround = true;
    wave = 0; kills = 0;
    waveTimer = 1.0f;
    timeScale = 1.0f;
    focusEnergy = 1.0f;
}
Grapple grap;
bool grappleWasDown = false, testGrapple = false, grappleAimOK = false;

glm::mat4 carWorld(int k, int i) {
    return roomToWorld() * trackFrame(tracks[k], carS(tracks[k], i), CAR_H * 0.5f + 0.1f);
}

// 시선 광선이 처음 닿는 곳: 달리는 객차(회전된 상자) 또는 고정 구조물
bool castGrapple(glm::vec3 o, glm::vec3 d, Grapple& out) {
    float best = 2.0f, tw;
    glm::vec3 p1 = o + d * stats.grappleRange;
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
                // 맞은 점을 객차 좌표계로 저장 → 객차가 달려도 줄은 그 점에 붙어 따라간다
                out.carLocal = glm::vec3(glm::inverse(carWorld(k, i)) * glm::vec4(out.anchor, 1.0f));
            }
        }
    return best <= 1.0f;
}

void releaseGrapple() { grap = Grapple{}; }

// 줄을 끊고 바라보는 방향으로 대쉬 (지금 그 방향 속도가 더 빠르면 유지)
void ropeDash() {
    glm::vec3 fwd = forwardDir();
    releaseGrapple();
    player.vel = fwd * std::max(stats.dash, glm::dot(player.vel, fwd));
    player.onGround = false;
}

void tryGrapple() {
    Grapple g;
    if (!castGrapple(eyePosition(), forwardDir(), g)) return;
    g.len = std::max(2.5f, glm::length(player.pos + glm::vec3(0, 1.5f, 0) - g.anchor));
    if (g.onTrain) {
        glm::mat4 C = carWorld(g.track, g.car);
        g.side = glm::dot(glm::vec3(C[2]), player.pos - glm::vec3(C[3])) >= 0.0f ? 1.0f : -1.0f;
        g.lastS = carS(tracks[g.track], g.car);
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
void travelTo(int id, glm::vec3 D, bool viaTrain) {
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
        if (viaTrain) {
            // 기차 구멍으로 들어왔으면 뒤쪽 벽에 같은 구멍이 열려 있고, 그 기세로 튀어나온다
            player.vel = h * 15.0f + glm::vec3(0, 4.0f, 0);
            glm::vec3 n(0.0f);
            int axis = fabsf(h.x) > fabsf(h.z) ? 0 : 2;
            n[axis] = h[axis] > 0.0f ? 1.0f : -1.0f;      // 방 안쪽 방향
            arrivalHolePos = player.pos + glm::vec3(0, 1.0f, 0);
            arrivalHolePos[axis] = -n[axis] * (H - 0.05f);
            arrivalHoleNormal = n;
            arrivalHoleColor = styleTint(styleForCell(id));
            arrivalHoleT = 1.0f;
        }
    }
    player.onGround = false;
    portalArmed = false;
    placeAugmentPickup();
    onEnterRoomTransform();
    onEnterRoomItems();
    bannerText = (id == HUB_CELL ? std::string("HUB") : "ROOM " + std::to_string(id)) + " - " + styleName(styleForCell(id));
    bannerT = 2.5f;
}

void restartRun() {
    resetAugments();
    resetTransform();
    resetItems();
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
    bannerText = "HUB - RIGHT CLICK A TRAIN TO HANG ON";
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

float fadeT = 0.0f;
glm::vec3 fadeColor(1.0f);
float arrivalHoleT = 0.0f;
glm::vec3 arrivalHolePos(0.0f), arrivalHoleNormal(0, 0, 1), arrivalHoleColor(1.0f);

// 기차에 줄로 매달려 간다: 줄이 걸린 점은 객차와 함께 움직이고, 플레이어는 중력 + 줄 길이 제약으로 진자처럼 끌려간다.
// 객차가 출구 벽의 구멍으로 들어가면 플레이어도 끌려 들어가 선로가 향하는 칸으로 넘어간다
void updateTrainRide(float dt) {
    const Track& t = tracks[grap.track];
    float s = carS(t, grap.car);
    if (s < grap.lastS - 5.0f) { releaseGrapple(); return; }    // 한 바퀴 돌아 처음으로 되돌아감 → 놓침
    grap.lastS = s;
    glm::mat4 C = carWorld(grap.track, grap.car);
    grap.anchor = glm::vec3(C * glm::vec4(grap.carLocal, 1.0f));
    // 줄을 감아 객차 쪽으로 올라붙되 매달린 거리는 남긴다. 낮은 선로에서는 발이 바닥에 끌리지 않게 줄을 더 짧게
    float target = glm::clamp(grap.anchor.y - 1.5f - 0.4f, 1.2f, RIDE_ROPE);
    grap.len = std::max(target, grap.len - 14.0f * dt);

    // 위치 기반 적분: 이전 위치와의 차이를 속도로 쓰면 움직이는 고정점에 매달려도 안정적이다
    glm::vec3 prev = player.pos;
    player.vel.y -= 18.0f * dt;
    player.vel *= 1.0f - 0.2f * dt;
    player.pos += player.vel * dt;
    glm::vec3 d = player.pos + glm::vec3(0, 1.5f, 0) - grap.anchor;
    float L = glm::length(d);
    if (L > grap.len) player.pos -= d / L * (L - grap.len);
    if (player.pos.y < 0.0f) player.pos.y = 0.0f;
    player.vel = (player.pos - prev) / std::max(dt, 1e-4f);
    player.onGround = false;

    // 출구 벽(구멍)에 닿으면 넘어간다. 그 전에는 방 밖으로 나가지 않게
    glm::vec3 local = glm::transpose(worldOrient) * (player.pos + glm::vec3(0, 1.0f, 0) - ROOM_C);
    glm::vec3 n = trackWallNormal(t, true);
    if (s > t.sMax - CAR_LEN && glm::dot(local, n) > ROOM_HALF - 1.5f) {
        int dest = trackDestination(t);
        if (dest >= 0) {
            fadeColor = styleTint(styleForCell(dest));
            fadeT = 1.0f;
            travelTo(dest, worldOrient * glm::round(t.dir), true);
            return;
        }
        releaseGrapple();
    }
    player.pos.x = glm::clamp(player.pos.x, -ROOM_HALF + PLAYER_R, ROOM_HALF - PLAYER_R);
    player.pos.z = glm::clamp(player.pos.z, -ROOM_HALF + PLAYER_R, ROOM_HALF - PLAYER_R);
}

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
    // 근접형: 달릴 때는 양팔을 앞뒤로 흔들고, 내려치기 준비 중에는 양팔을 머리 위로 든다
    float armR = !e.rusher ? aim : (e.atkT > 0.0f ? -PI * 0.95f : swing * 0.7f);
    if (e.rusher && e.atkT > 0.0f) r.arms[0] = glm::rotate(glm::translate(r.root, glm::vec3(-0.37f, 1.55f, 0)), -PI * 0.95f, glm::vec3(1, 0, 0));
    r.arms[1] = glm::rotate(glm::translate(r.root, glm::vec3(0.37f, 1.55f, 0)), armR, glm::vec3(1, 0, 0));
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
    onEnemyKilledItems(e.pos);
    if (stats.killsPerHeal > 0 && kills % stats.killsPerHeal == 0) player.hp = std::min(stats.maxHp, player.hp + 1);
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
        s.color = s.glow ? (e.rusher ? M_RUSHER_VISOR.color : M_VISOR.color)
                         : (frand(0, 1) < 0.5f ? (e.rusher ? M_RUSHER.color : M_ENEMY.color) : M_ENEMY_DARK.color);
        shards.push_back(s);
    }
    if (shards.size() > 700) shards.erase(shards.begin(), shards.begin() + (shards.size() - 700));
}

void damageEnemy(Enemy& e, float dmg, glm::vec3 hitDir) {
    if (!e.alive) return;
    e.hp -= dmg;
    if (e.hp <= 0.0f) killEnemy(e, hitDir);
}

void hurtPlayer() {
    if (player.dead || player.hurtT > 0.5f) return;    // 맞은 직후 약 0.3초 무적 (여럿에게 한꺼번에 맞아 즉사하지 않게)
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
    // 다중 사격: 가운데를 중심으로 좌우로 조금씩 벌려 쏜다
    glm::vec3 dir0 = glm::normalize(aim - muzzle);
    for (int i = 0; i < stats.pellets; ++i) {
        float a = (i - (stats.pellets - 1) * 0.5f) * 0.045f;
        glm::vec3 d = glm::normalize(dir0 * cosf(a) + right * sinf(a));
        Bullet b{ muzzle, d * stats.bulletSpeed, true, true, 3.0f };
        b.pierce = stats.pierce;
        bullets.push_back(b);
    }
    player.fireCd = stats.fireInterval;
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

    // 1인칭 스윙 연출: 빠를수록 시야각이 넓어지고, 줄에 매달리면 줄 쪽으로 화면이 기운다
    {
        float spd = glm::length(player.vel);
        glm::vec3 rightH(cosf(player.yaw), 0, sinf(player.yaw));
        float roll = 0.0f;
        if (grap.active && !grap.onTrain) {
            glm::vec3 toA = grap.anchor - eyePosition();
            toA.y = 0.0f;
            if (glm::length(toA) > 0.5f) roll = glm::dot(glm::normalize(toA), rightH) * 0.22f * std::min(1.0f, spd / 15.0f);
        }
        float fovAdd = player.onGround ? 0.0f : glm::clamp((spd - 8.0f) * 0.7f, 0.0f, 18.0f);
        float k = std::min(1.0f, 5.0f * dt);
        player.camRoll += (roll - player.camRoll) * k;
        player.camFov += (fovAdd - player.camFov) * k;
    }

    glm::vec3 f(sinf(player.yaw), 0, -cosf(player.yaw)), r(cosf(player.yaw), 0, sinf(player.yaw));
    glm::vec3 wish(0.0f);
    if (glfwGetKey(gWindow, GLFW_KEY_W) == GLFW_PRESS) wish += f;
    if (glfwGetKey(gWindow, GLFW_KEY_S) == GLFW_PRESS) wish -= f;
    if (glfwGetKey(gWindow, GLFW_KEY_D) == GLFW_PRESS) wish += r;
    if (glfwGetKey(gWindow, GLFW_KEY_A) == GLFW_PRESS) wish -= r;
    // 변신 중엔 Shift 도 달리기 (기본 상태의 Shift 는 불릿타임)
    bool shiftKey = glfwGetKey(gWindow, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
    bool sprint = glfwGetKey(gWindow, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || (capActive() && shiftKey);
    float speed = (sprint ? 8.5f * stats.sprintMul : 5.5f) * stats.moveMul;
    if (capActive() && cap.guarding) speed *= 0.5f;
    if (glm::length(wish) > 0.01f) wish = glm::normalize(wish) * speed;

    // 우클릭을 누르는 순간 줄을 쏘고, 떼면 놓는다
    bool hold = (glfwGetMouseButton(gWindow, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS || testGrapple) && !capActive();   // 변신 중 우클릭은 가드
    if (hold && !grappleWasDown) tryGrapple();
    if (!hold && grap.active) {
        // 스윙 중에 놓으면 진행 방향 + 위로 살짝 튕겨 나간다 (다음 줄을 걸 시간을 번다)
        if (!grap.onTrain && glm::length(player.vel) > 8.0f)
            player.vel += (glm::normalize(player.vel) * 3.0f + glm::vec3(0, 5.0f, 0)) * stats.releasePop;
        releaseGrapple();
    }
    grappleWasDown = hold;
    // 줄에 매달린 상태에서 Space를 누르는 순간 대쉬
    static bool spaceWasDown = false;
    bool space = glfwGetKey(gWindow, GLFW_KEY_SPACE) == GLFW_PRESS;
    if (space && !spaceWasDown && grap.active) ropeDash();
    spaceWasDown = space;
    if (grap.active && grap.onTrain) {
        updateTrainRide(dt);
        return;
    }

    glm::vec2 hv(player.vel.x, player.vel.z), target(wish.x, wish.z);
    if (player.onGround && slideT > 0.0f) {
        // 슬라이드: 조작은 안 듣고 미끄러지며 점점 느려진다
        slideT -= dt;
        hv = glm::vec2(slideVel.x, slideVel.z);
        slideVel *= std::max(0.0f, 1.0f - 1.6f * dt);
    } else if (player.onGround) {
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
        player.vel.y = stats.jump;
        player.onGround = false;
        slideT = 0.0f;      // 슬라이드 중 점프 = 캔슬 (미끄러지던 속도는 그대로 갖고 뜬다)
    }
    player.vel.y = std::max(player.vel.y - 18.0f * dt, -30.0f);

    player.pos += player.vel * dt;
    if (grap.active) {
        // 줄 제약: 손과 고정점 거리가 줄 길이를 넘지 못한다. 줄 길이는 건 순간 그대로, 진자처럼 흔들린다
        glm::vec3 d = player.pos + glm::vec3(0, 1.5f, 0) - grap.anchor;
        float L = glm::length(d);
        if (L > grap.len) {
            glm::vec3 n = d / L;
            player.pos -= n * (L - grap.len);
            float vr = glm::dot(player.vel, n);
            if (vr > 0.0f) player.vel -= n * vr;
            // 줄이 팽팽할 때: 중력을 더 걸어 묵직하게 떨어지고, 아래쪽 호에서 진행 방향으로 가속해 스윙이 죽지 않는다
            player.vel.y -= 10.0f * dt;
            glm::vec3 vt = player.vel - n * glm::dot(player.vel, n);
            float vtl = glm::length(vt);
            if (vtl > 1.0f && vtl < stats.swingMax) player.vel += vt / vtl * (stats.pump * std::max(0.0f, -n.y) * dt);
            // WASD 조종: 줄 방향 성분을 뺀 흔들림 방향으로만 힘을 준다 (줄을 당기거나 늘이지 않고 궤도만 바꿈)
            if (glm::length(wish) > 0.01f) {
                glm::vec3 wd = glm::normalize(wish);
                glm::vec3 wt = wd - n * glm::dot(wd, n);
                // 최고 속도를 넘으면 더 빨라지는 쪽으로는 안 밀고, 방향 트는 힘만 남긴다
                if (glm::length(player.vel) < stats.swingMax || glm::dot(player.vel, wt) < 0.0f) player.vel += wt * (stats.steer * dt);
                else player.vel += (wt - glm::normalize(player.vel) * glm::dot(wt, glm::normalize(player.vel))) * (stats.steer * dt);
            }
        }
        // 호 아래쪽에서 바닥에 닿기 직전일 때만 줄을 조금씩 짧게 해 발이 바닥 위로 지나가게 한다.
        // 한 번에 줄이면 먼 타점에 걸었을 때 앞으로 순간이동하므로 초당 12 까지만
        float ground = groundHeightAt(player.pos, PLAYER_R * 0.7f);
        float maxLen = grap.anchor.y - 1.5f - ground - 0.8f;
        glm::vec3 da = player.pos + glm::vec3(0, 1.5f, 0) - grap.anchor;
        bool nearBottom = da.y < -0.7f * glm::length(da) && player.pos.y - ground < 2.0f;
        if (nearBottom && maxLen > 2.5f && grap.len > maxLen) grap.len = std::max(maxLen, grap.len - 12.0f * dt);
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
        // 변신 중 Shift 를 누른 채 착지하면 그 기세로 슬라이드
        float hs = glm::length(glm::vec2(player.vel.x, player.vel.z));
        if (!wasGround && capActive() && shiftKey && hs > 3.0f) {
            slideT = 0.6f;
            slideVel = glm::vec3(player.vel.x, 0, player.vel.z) / hs * std::max(hs * 1.15f, 11.0f);
        }
    } else {
        player.onGround = false;
    }

    if (capActive()) { updateCaptain(dt); return; }
    bool fire = glfwGetMouseButton(gWindow, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    if (fire && player.fireCd <= 0.0f) firePlayer();
}

void updateEnemies(float dt) {
    int enemyBullets = 0;
    for (const auto& b : bullets) enemyBullets += b.alive && !b.fromPlayer;
    glm::vec3 head = player.pos + glm::vec3(0, 1.45f, 0);
    std::vector<glm::vec3> before(enemies.size());
    for (size_t i = 0; i < enemies.size(); ++i) before[i] = enemies[i].pos;
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
        if (glm::length(move) > 0.01f && !player.dead && !e.rusher) {
            e.pos += glm::normalize(move) * 2.3f * dt;
            e.walkT += dt;
        }
        for (const auto& b : boxes)
            if (b.solid && b.bmax().y > e.pos.y + STEP_H && b.bmin().y < e.pos.y + ENEMY_H) pushOutCircle(e.pos, ENEMY_R, b);

        if (e.rusher) {
            // 근접형: 곧장 달려와서, 가까우면 팔을 들고(0.4초 준비) 내려친다. 그 사이 피하면 빗나간다
            float vdist = fabsf(player.pos.y - e.pos.y);
            if (e.atkT > 0.0f) {
                e.atkT -= dt;
                if (e.atkT <= 0.0f) {
                    e.atkT = 0.0f;
                    e.atkCd = 1.0f;
                    if (!player.dead && dist < 2.0f && vdist < 1.6f) {
                        if (capGuardBlocks(dir)) guardHit();    // 방패 가드로 막음
                        else hurtPlayer();
                    }
                    spawnSparks(e.pos + dir * 0.8f + glm::vec3(0, 1.0f, 0), glm::vec3(1.0f, 0.5f, 0.1f), 8, 3.0f);
                }
            } else if (!player.dead) {
                e.atkCd -= dt;
                if (dist < 1.5f && vdist < 1.6f && e.atkCd <= 0.0f) e.atkT = 0.4f;
                else if (dist > 1.1f) {
                    e.pos += dir * 5.2f * dt;
                    e.walkT += dt * 1.6f;
                }
            }
            for (const auto& b : boxes)
                if (b.solid && b.bmax().y > e.pos.y + STEP_H && b.bmin().y < e.pos.y + ENEMY_H) pushOutCircle(e.pos, ENEMY_R, b);
            continue;
        }

        glm::vec3 muzzle = enemyMuzzle(e);
        bool see = !segmentBlocked(muzzle, head);
        e.aimT = glm::clamp(e.aimT + (see ? dt * 2.0f : -dt), 0.0f, 1.0f);
        e.shootCd -= dt;
        if (e.shootCd <= 0.0f && see && !player.dead && e.aimT > 0.8f && enemyBullets < ENEMY_BULLET_CAP) {
            ++enemyBullets;
            glm::vec3 aim = player.pos + glm::vec3(0, 1.25f, 0) + glm::vec3(frand(-0.4f, 0.4f), frand(-0.3f, 0.3f), frand(-0.4f, 0.4f));
            bullets.push_back({ muzzle, glm::normalize(aim - muzzle) * 15.0f, false, true, 6.0f });
            e.shootCd = frand(2.5f, 5.0f);
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
    for (size_t i = 0; i < before.size(); ++i) enemies[i].vel = dt > 1e-5f ? (enemies[i].pos - before[i]) / dt : glm::vec3(0.0f);
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
                    if (b.pierce > 0) --b.pierce;   // 관통: 이 적을 지나 계속 날아간다
                    else b.alive = false;
                    break;
                }
            }
        } else if (!player.dead) {
            glm::vec3 end = p0 + (p1 - p0) * tHitT;
            if (segHitsCylinder(p0, end, player.pos, PLAYER_R, PLAYER_H)) {
                if (capGuardBlocks(dir)) {
                    // 방패 가드: 바라보는 방향으로 되쏘아 보낸다 (적도 맞힌다)
                    guardHit();
                    b.fromPlayer = true;
                    b.vel = forwardDir() * 45.0f;
                    b.pos = eyePosition() + forwardDir() * 0.8f;
                    b.life = 3.0f;
                    spawnSparks(b.pos, glm::vec3(0.8f, 0.9f, 1.0f), 8, 3.0f);
                    continue;
                }
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

void update(float realDt) {
    globalTime += realDt;
    fadeT = std::max(0.0f, fadeT - realDt * 1.4f);
    arrivalHoleT = std::max(0.0f, arrivalHoleT - realDt * 0.7f);
    if (cubeView) { updateCubeView(realDt); return; }
    if (augPicking) return;     // 증강 고르는 동안 멈춤
    if (rotAnimActive) {
        rotAnimT += realDt / 1.1f;
        if (rotAnimT >= 1.0f) rotAnimActive = false;
        return;
    }
    helpT -= realDt;
    waveBannerT -= realDt;
    bannerT -= realDt;

    // 집중 모드: 세계 시간은 크게 느려지고, 플레이어는 덜 느려짐
    focusActive = !player.dead && !capActive() && focusEnergy > 0.02f &&
                  glfwGetKey(gWindow, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
    if (focusActive) focusEnergy = std::max(0.0f, focusEnergy - realDt * stats.focusDrain);
    else focusEnergy = std::min(1.0f, focusEnergy + realDt * stats.focusRegen);
    float targetScale = player.dead ? 0.08f : (focusActive ? 0.1f : 1.0f);
    timeScale += (targetScale - timeScale) * std::min(1.0f, realDt * 8.0f);

    float worldDt = realDt * timeScale;
    float playerDt = realDt * (0.45f + 0.55f * timeScale);

    trainTime += worldDt;
    updatePlayer(playerDt);
    updateShield(playerDt);
    updateArrow(worldDt);
    updateWheel();
    encounterBannerT -= realDt;
    updatePortals();
    updateEnemies(worldDt);
    updateBullets(worldDt);
    updateDebris(worldDt);

    // 정해진 웨이브를 다 막으면 방 클리어 → 이 방의 포털이 열린다
    if (!roomCleared[currentCell] && !player.dead) {
        if (wave >= ROOM_WAVES && enemies.empty()) {
            roomCleared[currentCell] = true;
            bannerText = "ROOM CLEAR - PORTALS OPEN";
            bannerT = 3.0f;
            player.hp = std::min(stats.maxHp, player.hp + stats.clearHeal);
            bannerText = "ROOM CLEAR - TAKE THE AUGMENT [F]";
            placeAugmentPickup();
            maybeStartEncounter();
        } else if (wave < ROOM_WAVES && (int)enemies.size() <= WAVE_NEXT_AT) {
            waveTimer -= realDt;
            if (waveTimer <= 0.0f) {
                spawnWave();
                waveTimer = 1.5f;
            }
        }
    }
    Grapple probe;
    grappleAimOK = !grap.active && !player.dead && !capActive() && castGrapple(eyePosition(), forwardDir(), probe);
}
