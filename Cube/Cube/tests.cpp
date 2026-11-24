// 확인용 모드: --shot 스크립트 테스트, --bench 성능 측정
#include "cube.h"

   // 시작 직후 드라이버 준비(셰이더·텍스처 업로드) 시간이 섞이지 않도록 예열을 넉넉히

void benchSetup(const BenchScene& s) {
    if (s.cell == 24) turnSlice(1, 1, 1);   // 윗층을 돌려 24번 방의 방향을 바꿔 둔다
    travelTo(s.cell, glm::vec3(0.0f));
    roomCleared[s.cell] = !s.enemies;
    if (s.enemies) { spawnWave(); spawnWave(); waveTimer = 1e9f; }
    std::mt19937 r(1234);
    std::uniform_real_distribution<float> u(-ROOM_HALF + 1.0f, ROOM_HALF - 1.0f), sz(0.5f, 2.0f);
    for (int i = 0; i < s.extraBoxes; ++i) {
        glm::vec3 c(u(r), u(r) + ROOM_HALF, u(r));
        if (glm::length(glm::vec2(c.x, c.z)) < 4.0f && c.y < 4.0f) continue;   // 카메라 자리는 비움
        boxes.push_back({ c, glm::vec3(sz(r), sz(r), sz(r)), M_CRATE, false });
    }
    player.hp = 1000000;
    player.pos = s.pos;
}

// 매 프레임 루프 맨 앞에서 호출. 끝나면 false
bool benchStep(float frameMs) {
    static int scene = -1, f = 0;
    static std::vector<float> ms;
    static long long dcSum = 0, triSum = 0, lightSum = 0;
    static std::vector<std::string> report;
    const int nScenes = (int)(sizeof(BENCH) / sizeof(BENCH[0]));
    if (scene < 0) { scene = 0; f = 0; benchSetup(BENCH[0]); }
    const BenchScene& s = BENCH[scene];
    ++f;
    if (f > BENCH_WARM) {
        ms.push_back(frameMs);
        dcSum += statDrawCalls;
        triSum += statTriangles;
        lightSum += statLights;
    }
    if (f >= BENCH_WARM + BENCH_MEAS) {
        std::vector<float> sorted = ms;
        std::sort(sorted.begin(), sorted.end());
        float avg = std::accumulate(ms.begin(), ms.end(), 0.0f) / ms.size();
        float p99 = sorted[(size_t)(sorted.size() * 0.99f)];
        int n = (int)ms.size();
        int w, h;
        glfwGetFramebufferSize(gWindow, &w, &h);
        char line[256];
        snprintf(line, sizeof(line), "%-22s %4dx%-4d avg %6.2f ms (%5.0f fps)  1%%low %6.2f ms (%5.0f fps)  draws %6lld  tris %8lld  lights %2lld  boxes %5d",
                 s.name, w, h, avg, 1000.0f / avg, p99, 1000.0f / p99, dcSum / n, triSum / n, lightSum / n, (int)boxes.size());
        std::cout << "[bench] " << line << "\n";
        report.push_back(line);
        ms.clear();
        dcSum = triSum = lightSum = 0;
        ++scene;
        f = 0;
        if (scene >= nScenes) {
            std::ofstream out("bench_result.txt");
            out << "GPU " << (const char*)glGetString(GL_RENDERER) << "\n";
            for (auto& l : report) out << l << "\n";
            return false;
        }
        initCube();
        benchSetup(BENCH[scene]);
    }
    // 카메라 고정 위치에서 한 바퀴 회전
    player.pos = s.pos;
    player.vel = glm::vec3(0.0f);
    player.onGround = true;
    player.yaw = (float)f / (BENCH_WARM + BENCH_MEAS) * 2.0f * PI;
    player.pitch = s.pitch;
    return true;
}

void saveScreenshot(int w, int h, const std::string& path) {
    std::vector<unsigned char> px((size_t)w * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << w << " " << h << "\n255\n";
    for (int y = h - 1; y >= 0; --y) out.write((const char*)&px[(size_t)y * w * 3], (std::streamsize)w * 3);
}

// --shot 실행 시 실제 게임 함수를 스크립트로 불러 허브 → 기차 → 방 → 클리어 → 포털 → 회전 → 재시작을 확인
void aimAt(glm::vec3 p) {
    glm::vec3 d = glm::normalize(p - eyePosition());
    player.yaw = atan2f(d.x, -d.z);
    player.pitch = asinf(glm::clamp(d.y, -1.0f, 1.0f));
}

void shotScript(int frame, int w, int h) {
    auto shot = [&](int n) { saveScreenshot(w, h, "shot_" + std::to_string(n) + ".ppm"); };
    auto pv = [](glm::vec3 p) { return std::to_string((int)p.x) + "," + std::to_string((int)p.y) + "," + std::to_string((int)p.z); };
    static int phase = 0, t0 = 0, rideCell = -1;
    static glm::vec3 portalDir(0.0f);
    // 방 클리어 → 증강 후보 3개(서로 다르고, 가질 수 있는 단계만)를 확인하고 첫 번째를 고른다. 바로 Tab 화면도 한 장
    static int tabShotFrame = -1;
    if (frame == tabShotFrame) openCubeView();
    if (frame == tabShotFrame + 1) { shot(31); closeCubeView(); }
    if (augPicking) {
        static bool picShot = false;
        if (!picShot) { shot(30); picShot = true; }
        bool ok = augOfferCount == 3 && augOffer[0] != augOffer[1] && augOffer[1] != augOffer[2] && augOffer[0] != augOffer[2];
        std::cout << "[test] augment offer:";
        for (int k = 0; k < augOfferCount; ++k) {
            const Augment& a = augments[augOffer[k]];
            std::cout << " [" << a.name << "]";
            ok = ok && augLineTier(a.line) == a.tier - 1;
        }
        int stat = 0;
        for (const auto& a : augments) stat += !a.skill;
        std::cout << " pool " << augments.size() << " stat " << stat << (ok ? "" : " FAIL") << "\n";
        float before = stats.fireInterval + stats.bulletSpeed + stats.maxHp + stats.grappleRange + stats.dash + stats.jump + stats.moveMul;
        chooseAugment(0);
        tabShotFrame = frame + 1;
        float after = stats.fireInterval + stats.bulletSpeed + stats.maxHp + stats.grappleRange + stats.dash + stats.jump + stats.moveMul;
        std::cout << "[test] augment picked " << augments[ownedAugments.back()].name << " owned " << ownedAugments.size()
                  << " paused-then-resumed " << !augPicking << " stats changed " << (before != after || stats.pierce || stats.pellets > 1 || stats.clearHeal || stats.killsPerHeal || stats.focusDrain != 0.22f || stats.focusRegen != 0.12f || stats.pump != 10.0f || stats.steer != 8.0f || stats.releasePop != 1.0f || stats.sprintMul != 1.0f) << "\n";
    }
    if (frame == 30) { player.pitch = 0.3f; player.yaw = 0.6f; }
    if (frame == 59) {
        // 첫 장면은 가장 가까운 객차를 바라보게 한다
        float best = 1e9f;
        glm::vec3 look(0.0f);
        for (int k = 0; k < (int)tracks.size(); ++k)
            for (int i = 0; i < tracks[k].cars; ++i) {
                float s = carS(tracks[k], i);
                if (s < tracks[k].sMin + 3.0f || s > tracks[k].sMax - 3.0f) continue;
                glm::vec3 c = glm::vec3(carWorld(k, i)[3]);
                float d = glm::length(c - eyePosition());
                if (d < best && d > 12.0f) { best = d; look = c; }
            }
        if (best < 1e9f) aimAt(look);
    }
    if (frame == 60) {
        shot(0);
        std::cout << "[test] start cell " << currentCell << " hub " << (currentCell == HUB_CELL) << " tracks " << tracks.size()
                  << " decor " << localDecor.size() << " boxes " << boxes.size() << "\n";
    }
    if (phase == 0 && frame >= 61 && frame % 10 == 1 && !grap.active) {
        // 시야가 트인 객차 중 벽까지 충분히 남은 것을 골라 조준하고 Shift
        for (int k = 0; k < (int)tracks.size() && !grap.active; ++k)
            for (int i = 0; i < tracks[k].cars; ++i) {
                float s = carS(tracks[k], i);
                if (s < tracks[k].sMin + 4.0f || s > tracks[k].sMax - 25.0f) continue;
                glm::vec3 c = glm::vec3(carWorld(k, i)[3]);
                Grapple g;
                if (!castGrapple(eyePosition(), glm::normalize(c - eyePosition()), g) || !g.onTrain) continue;
                aimAt(c);
                testGrapple = false;
                grappleWasDown = false;
                tryGrapple();
                testGrapple = true;
                std::cout << "[test] grapple at frame " << frame << " active " << grap.active << " onTrain " << grap.onTrain
                          << " track " << grap.track << " car " << grap.car << "\n";
                break;
            }
    }
    if (phase == 0 && grap.onTrain && rideCell < 0) { rideCell = 1; t0 = frame; }
    static bool holeShot = false;
    if (phase == 0 && rideCell == 1 && frame == t0 + 40) {
        aimAt(grap.anchor);
        shot(1);
        std::cout << "[test] riding, player " << pv(player.pos) << " anchor " << pv(grap.anchor)
                  << " rope " << glm::length(player.pos + glm::vec3(0, 1.5f, 0) - grap.anchor) << " speed " << glm::length(player.vel) << "\n";
    }
    if (phase == 0 && grap.onTrain && !holeShot) {
        // 출구 구멍에 가까워지면 그 구멍을 바라보고 한 장
        const Track& t = tracks[grap.track];
        glm::vec3 n = trackWallNormal(t, true);
        glm::vec3 local = glm::transpose(worldOrient) * (player.pos - ROOM_C);
        if (glm::dot(local, n) > ROOM_HALF - 14.0f) {
            glm::vec3 hole = glm::vec3(roomToWorld() * glm::vec4(trackWallPoint(t, true) + glm::vec3(0, CAR_H * 0.5f - 1.2f, 0), 1.0f));
            aimAt(hole);
            shot(7);
            holeShot = true;
            std::cout << "[test] approaching exit hole, open " << holeOpen(t, true) << " dest " << trackDestination(t) << "\n";
        }
    }
    if (phase == 0 && currentCell != HUB_CELL) {
        phase = 1; t0 = frame; testGrapple = false;
        std::cout << "[test] arrival hole " << arrivalHoleT << " fade " << fadeT << " style " << styleName(styleForCell(currentCell)) << "\n";
        std::cout << "[test] train carried player to cell " << currentCell << " pos " << pv(player.pos) << " cleared " << roomCleared[currentCell] << "\n";
    }
    if (phase == 0 && frame > 2500) { std::cout << "[test] FAIL train ride timeout\n"; glfwSetWindowShouldClose(gWindow, true); }

    if (phase == 1 && frame == t0 + 12) {
        aimAt(arrivalHolePos);     // 도착한 방: 뒤쪽 벽에 열렸다 닫히는 구멍
        shot(8);
        std::cout << "[test] arrival hole open " << arrivalHoleT << "\n";
    }
    if (phase == 1 && frame == t0 + 150) {
        std::cout << "[test] room after 2.5s: wave " << wave << " enemies " << enemies.size() << " onGround " << player.onGround << " y " << player.pos.y << "\n";
        aimAt(ROOM_C);
        shot(2);
        // 방 강제 클리어: 마지막 웨이브까지 진행된 것으로 하고 적을 모두 처치
        wave = ROOM_WAVES;
        for (auto& e : enemies) killEnemy(e, glm::vec3(0, 0, 1));
        enemies.clear();
    }
    if (phase == 1 && frame == t0 + 160) {
        std::cout << "[test] cleared " << roomCleared[currentCell] << " portals:";
        for (const auto& D : FACE_DIRS) std::cout << " (" << pv(D) << ")->" << portalTarget(D);
        std::cout << "\n";
        // 열린 수평 포털 하나를 골라 그 앞 공중에서 포털에 줄을 건다
        for (const auto& D : FACE_DIRS)
            if (fabsf(D.y) < 0.5f && portalTarget(D) >= 0 && portalTarget(D) != HUB_CELL) { portalDir = D; break; }
        player.pos = ROOM_C + portalDir * (ROOM_HALF - 10.0f) - glm::vec3(0, 1.5f, 0);
        player.vel = glm::vec3(0.0f);
        aimAt(ROOM_C + portalDir * ROOM_HALF);
        grappleWasDown = false;
        testGrapple = true;
    }
    if (phase == 1 && frame == t0 + 163) {
        std::cout << "[test] portal grapple active " << grap.active << " onTrain " << grap.onTrain << " anchor " << pv(grap.anchor)
                  << " target " << portalTarget(portalDir) << "\n";
        shot(3);
    }
    // 줄 길이가 고정이라 포털까지 당겨지지 않는다 → 줄에 매달린 채 포털 쪽으로 대쉬
    if (phase == 1 && frame == t0 + 170 && grap.active) { aimAt(ROOM_C + portalDir * ROOM_HALF); ropeDash(); }
    static int roomBefore = -1;
    if (phase == 1 && frame == t0 + 159) roomBefore = currentCell;
    if (phase == 1 && frame > t0 + 163 && currentCell != roomBefore) {
        phase = 2; t0 = frame; testGrapple = false;
        std::cout << "[test] portal carried player " << roomBefore << " -> " << currentCell << "\n";
    }
    if (phase == 1 && frame > t0 + 600) { std::cout << "[test] FAIL portal timeout, player " << pv(player.pos) << "\n"; glfwSetWindowShouldClose(gWindow, true); }

    if (phase == 2 && frame == t0 + 40) {
        shot(4);
        // 큐브 회전으로 포털 너머의 방이 바뀌는지: 지금 방은 그대로 두고 옆 칸이 있는 면을 돌린다
        glm::ivec3 cur = cubies[currentCell].pos;
        for (int ax = 0; ax < 3; ++ax) {
            // 옆 칸이 회전축 위에 있으면 돌아도 제자리라 의미가 없다
            if (cur[(ax + 1) % 3] == 0 && cur[(ax + 2) % 3] == 0) continue;
            glm::vec3 D(0.0f);
            D[ax] = cur[ax] < 1 ? 1.0f : -1.0f;
            int before = portalTarget(D);
            int layer = cur[ax] + (int)D[ax];   // 옆 칸이 속한 층. 지금 칸은 이 층에 없으므로 지금 방은 돌지 않는다
            turnSlice(ax, layer, 1);
            int after = portalTarget(D);
            std::cout << "[test] turn axis " << ax << " layer " << layer << ": portal (" << pv(D) << ") " << before << " -> " << after
                      << " current cell moved " << (cubies[currentCell].pos != cur) << "\n";
            break;
        }
    }
    // 스타일별 방 한 장씩: 툰, 픽셀, 누아르, 네온, 청사진, 미니멀(26), 실사(허브)
    static const int styleCells[7] = { 5, 6, 7, 8, 9, ARENA_CELL, HUB_CELL };
    for (int k = 0; k < 7; ++k) {
        if (phase == 2 && frame == t0 + 50 + k * 30) {
            roomCleared[styleCells[k]] = true;
            travelTo(styleCells[k], glm::vec3(0.0f));
            fadeT = 0.0f;
            player.pos = glm::vec3(-ROOM_HALF * 0.6f, 0.0f, ROOM_HALF * 0.6f);
            aimAt(glm::vec3(ROOM_HALF * 0.3f, 6.0f, -ROOM_HALF * 0.3f));
        }
        if (phase == 2 && frame == t0 + 50 + k * 30 + 25) {
            bannerT = 0.0f;
            shot(10 + k);
            std::cout << "[test] style shot cell " << styleCells[k] << " " << styleName(styleForCell(styleCells[k])) << "\n";
        }
    }
    if (phase == 2 && frame == t0 + 300) {
        restartRun();
        std::cout << "[test] restart -> cell " << currentCell << " cleared rooms " << std::count(std::begin(roomCleared), std::end(roomCleared), true) << "\n";
    }
    if (phase == 2 && frame == t0 + 315) {
        std::cout << "[test] done\n";
        glfwSetWindowShouldClose(gWindow, true);
    }
}
