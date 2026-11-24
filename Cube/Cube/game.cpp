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
Grapple grap;
bool grappleWasDown = false, testGrapple = false, grappleAimOK = false;

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
                // 맞은 점을 객차 좌표계로 저장 → 객차가 달려도 줄은 그 점에 붙어 따라간다
                out.carLocal = glm::vec3(glm::inverse(carWorld(k, i)) * glm::vec4(out.anchor, 1.0f));
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
    bannerText = (id == HUB_CELL ? std::string("HUB") : "ROOM " + std::to_string(id)) + " - " + styleName(styleForCell(id));
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

    // 우클릭을 누르는 순간 줄을 쏘고, 떼면 놓는다
    bool hold = glfwGetMouseButton(gWindow, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS || testGrapple;
    if (hold && !grappleWasDown) tryGrapple();
    if (!hold && grap.active) releaseGrapple();
    grappleWasDown = hold;
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

void update(float realDt) {
    globalTime += realDt;
    fadeT = std::max(0.0f, fadeT - realDt * 1.4f);
    arrivalHoleT = std::max(0.0f, arrivalHoleT - realDt * 0.7f);
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
                  glfwGetKey(gWindow, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
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
