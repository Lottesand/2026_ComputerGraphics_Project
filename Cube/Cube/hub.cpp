// 허브 기차역: 선로, 기차, 장식
#include "cube.h"

// 방 좌표계 → 월드: 방 중심을 기준으로 현재 방향(worldOrient)만큼 회전
glm::mat4 roomToWorld() {
    return glm::translate(glm::mat4(1.0f), ROOM_C) * glm::mat4(worldOrient) * glm::translate(glm::mat4(1.0f), -ROOM_C);
}
std::vector<Decor> localDecor;
std::vector<Track> tracks;
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

// 선로가 벽과 만나는 점 (방 좌표계). exitSide 면 기차가 들어가는 쪽(sMax), 아니면 나오는 쪽(sMin)
glm::vec3 trackWallPoint(const Track& t, bool exitSide) {
    glm::vec3 p = t.c + t.dir * (exitSide ? t.sMax : t.sMin);
    int axis = fabsf(p.x) > fabsf(p.z) ? 0 : 2;
    p[axis] = (p[axis] > 0.0f ? 1.0f : -1.0f) * ROOM_HALF;
    return p;
}

// 그 벽의 바깥 방향 (방 좌표계). 대각선 선로는 먼저 닿는 벽
glm::vec3 trackWallNormal(const Track& t, bool exitSide) {
    glm::vec3 p = t.c + t.dir * (exitSide ? t.sMax : t.sMin), n(0.0f);
    int axis = fabsf(p.x) > fabsf(p.z) ? 0 : 2;
    n[axis] = p[axis] > 0.0f ? 1.0f : -1.0f;
    return n;
}

// 선로가 향하는 칸: 방 회전을 적용한 진행 방향으로 이웃 칸을 찾는다 (대각선이면 (±1, 0, ±1))
int trackDestination(const Track& t) {
    glm::vec3 D = worldOrient * glm::round(t.dir);
    return cubieAt(cubies[currentCell].pos + glm::ivec3(glm::round(D)));
}

// 벽 구멍이 열린 정도 0~1: 기차 머리가 다가오면 열리고, 꼬리가 다 지나가면 닫힌다
float holeOpen(const Track& t, bool exitSide) {
    float head = carS(t, 0), tail = carS(t, t.cars - 1);
    float edge = exitSide ? t.sMax : t.sMin;
    float a = glm::clamp((head - (edge - (exitSide ? 32.0f : 10.0f))) / 10.0f, 0.0f, 1.0f);
    float b = glm::clamp(((edge + CAR_LEN + 6.0f) - tail) / 6.0f, 0.0f, 1.0f);
    return a * b;
}
