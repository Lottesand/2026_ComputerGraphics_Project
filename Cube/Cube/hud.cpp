// HUD: 2D 동차좌표 변환과 비트맵 글꼴
#include "cube.h"

const unsigned char* glyphFor(char ch) {
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
    for (const auto& g : FONT)
        if (g.c == ch) return g.rows;
    return nullptr;
}

float textWidth(const std::string& s, float scale) {
    return s.empty() ? 0.0f : (s.size() * 6.0f - 1.0f) * scale;
}

void drawText(Batch2D& b, float x, float y, float s, const std::string& str, const glm::vec4& col) {
    for (int pass = 0; pass < 2; ++pass) {
        glm::vec4 c = pass == 0 ? glm::vec4(0, 0, 0, col.a * 0.5f) : col;
        float off = pass == 0 ? s * 0.5f : 0.0f;
        float cx = x;
        for (char ch : str) {
            const unsigned char* g = glyphFor(ch);
            if (g)
                for (int r = 0; r < 7; ++r)
                    for (int k = 0; k < 5; ++k)
                        if (g[r] & (0x10 >> k)) b.rect(cx + k * s + off, y + (6 - r) * s - off, s, s, c);
            cx += 6.0f * s;
        }
    }
}

void drawTextCentered(Batch2D& b, float cx, float y, float s, const std::string& str, const glm::vec4& col) {
    drawText(b, cx - textWidth(str, s) * 0.5f, y, s, str, col);
}

// 폭에 맞게 단어 단위로 줄바꿈
static std::vector<std::string> wrapText(const std::string& s, float maxW, float scale) {
    std::vector<std::string> lines;
    std::string cur, word;
    auto flush = [&]() {
        if (word.empty()) return;
        std::string t = cur.empty() ? word : cur + " " + word;
        if (!cur.empty() && textWidth(t, scale) > maxW) { lines.push_back(cur); cur = word; }
        else cur = t;
        word.clear();
    };
    for (char c : s) { if (c == ' ') flush(); else word += c; }
    flush();
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

// 계열 단계를 작은 칸으로: 가진 단계는 채우고 남은 단계는 빈 칸
static void drawPips(Batch2D& b, float x, float y, float sz, int have, int total, const glm::vec4& on) {
    for (int i = 0; i < total; ++i)
        b.rect(x + i * sz * 1.5f, y, sz, sz, i < have ? on : glm::vec4(1, 1, 1, 0.18f));
}

// 방 클리어 후 증강 3개 중 하나 고르기 (게임은 멈춰 있다)
static void drawAugmentPicker(Batch2D& b, int w, int h) {
    float ui = h / 720.0f;
    b.rect(0, 0, (float)w, (float)h, glm::vec4(0, 0, 0, 0.62f));
    drawTextCentered(b, w * 0.5f, h * 0.8f, 3.2f * ui, "ROOM CLEAR - CHOOSE AN AUGMENT", glm::vec4(0.6f, 1.0f, 1.0f, 1.0f));
    drawTextCentered(b, w * 0.5f, h * 0.8f - 30 * ui, 1.6f * ui, "CLICK A CARD OR PRESS 1  2  3", glm::vec4(1, 1, 1, 0.7f));
    double mx, my;
    int ww, wh;
    glfwGetCursorPos(gWindow, &mx, &my);
    glfwGetWindowSize(gWindow, &ww, &wh);
    int hover = ww > 0 && wh > 0 ? augmentCardAt(mx * w / ww, h - my * h / wh, w, h) : -1;
    for (int i = 0; i < augOfferCount; ++i) {
        const Augment& a = augments[augOffer[i]];
        float x, y, cw, ch;
        augmentCardRect(i, w, h, x, y, cw, ch);
        bool hv = i == hover;
        glm::vec4 edge = hv ? glm::vec4(1.0f, 0.85f, 0.2f, 1.0f) : glm::vec4(0.3f, 0.9f, 1.0f, 0.8f);
        float t = (hv ? 3.0f : 2.0f) * ui;
        b.rect(x - t, y - t, cw + 2 * t, ch + 2 * t, edge);
        b.rect(x, y, cw, ch, hv ? glm::vec4(0.12f, 0.14f, 0.2f, 0.97f) : glm::vec4(0.06f, 0.08f, 0.12f, 0.95f));
        float pad = 14 * ui, top = y + ch - pad;
        drawText(b, x + pad, top - 10 * ui, 1.5f * ui, "[" + std::to_string(i + 1) + "]  " + (a.skill ? "SKILL" : "STAT"), glm::vec4(1, 1, 1, 0.55f));
        std::vector<std::string> nm = wrapText(a.name, cw - 2 * pad, 2.4f * ui);
        float ty = top - 46 * ui;
        for (const auto& l : nm) { drawText(b, x + pad, ty, 2.4f * ui, l, edge); ty -= 24 * ui; }
        drawPips(b, x + pad, ty + 4 * ui, 8 * ui, a.tier, augLineTiers(a.line), edge);
        ty -= 26 * ui;
        for (const auto& l : wrapText(a.desc, cw - 2 * pad, 1.5f * ui)) { drawText(b, x + pad, ty, 1.5f * ui, l, glm::vec4(1, 1, 1, 0.9f)); ty -= 16 * ui; }
    }
}

// 인벤토리 휠 (C 꾹): 8칸, 맨 위가 0 번이고 시계방향. 마우스를 민 방향의 칸이 밝아진다
static void drawWheel(Batch2D& b, int w, int h) {
    float ui = h / 720.0f, cx = w * 0.5f, cy = h * 0.5f, R0 = 70.0f * ui, R1 = 230.0f * ui;
    auto P = [&](float a, float r) { return glm::vec2(cx + sinf(a) * r, cy + cosf(a) * r); };
    for (int i = 0; i < 8; ++i) {
        float a0 = (i - 0.5f) * PI / 4.0f, a1 = (i + 0.5f) * PI / 4.0f;
        glm::vec4 fill = i == wheelSel ? glm::vec4(1, 1, 1, 0.28f) : glm::vec4(0, 0, 0, 0.42f);
        for (int k = 0; k < 6; ++k) {
            float u0 = a0 + (a1 - a0) * k / 6.0f, u1 = a0 + (a1 - a0) * (k + 1) / 6.0f;
            b.tri(P(u0, R0), P(u0, R1), P(u1, R1), fill);
            b.tri(P(u0, R0), P(u1, R1), P(u1, R0), fill);
        }
        // 칸 사이 경계선
        glm::vec2 e0 = P(a0, R0), e1 = P(a0, R1), n = glm::normalize(glm::vec2(e1.y - e0.y, e0.x - e1.x)) * (1.2f * ui);
        b.tri(e0 - n, e1 - n, e1 + n, glm::vec4(1, 1, 1, 0.6f));
        b.tri(e0 - n, e1 + n, e0 + n, glm::vec4(1, 1, 1, 0.6f));
        glm::vec2 lp = P(i * PI / 4.0f, (R0 + R1) * 0.5f);
        bool has = invSlots[i] >= 0;
        std::string label = has ? ITEM_NAMES[invSlots[i]] : "EMPTY";
        glm::vec4 col = has ? (i == wheelSel ? glm::vec4(1.0f, 0.85f, 0.3f, 1.0f) : glm::vec4(1, 1, 1, 0.95f)) : glm::vec4(1, 1, 1, 0.3f);
        float s = 1.3f * ui;
        if (label.size() > 10) {
            size_t sp = label.find(' ');
            if (sp != std::string::npos) {
                drawTextCentered(b, lp.x, lp.y + 2 * ui, s, label.substr(0, sp), col);
                drawTextCentered(b, lp.x, lp.y - 12 * ui, s, label.substr(sp + 1), col);
                continue;
            }
        }
        drawTextCentered(b, lp.x, lp.y - 4 * ui, s, label, col);
    }
    // 가운데 고리
    for (int k = 0; k < 48; ++k) {
        float u0 = 2.0f * PI * k / 48.0f, u1 = 2.0f * PI * (k + 1) / 48.0f;
        b.tri(P(u0, R0 - 6 * ui), P(u0, R0), P(u1, R0), glm::vec4(1, 1, 1, 0.85f));
        b.tri(P(u0, R0 - 6 * ui), P(u1, R0), P(u1, R0 - 6 * ui), glm::vec4(1, 1, 1, 0.85f));
    }
    drawTextCentered(b, cx, cy - 5 * ui, 1.4f * ui, "ITEMS", glm::vec4(1, 1, 1, 0.8f));
}

// 캡틴 HUD: 가드 내구도 칸, E/Q 쿨타임, 방패 상태
static void drawCapHUD(Batch2D& b, int w, int h) {
    float ui = h / 720.0f, cx = w * 0.5f;
    glm::vec4 blue(0.35f, 0.6f, 1.0f, 1.0f), red(1.0f, 0.3f, 0.25f, 1.0f), white(1, 1, 1, 0.9f);
    float sw = 34.0f * ui, sh = 8.0f * ui, gap = 5.0f * ui, total = GUARD_MAX * sw + (GUARD_MAX - 1) * gap, by = 34.0f * ui;
    bool broken = cap.guardLock > 1.0f;
    for (int i = 0; i < GUARD_MAX; ++i)
        b.rect(cx - total * 0.5f + i * (sw + gap), by, sw, sh, broken ? glm::vec4(0.3f, 0.1f, 0.1f, 0.7f)
                                                               : (i < GUARD_MAX - cap.guardHits ? (cap.guarding ? white : blue) : glm::vec4(0, 0, 0, 0.45f)));
    std::string gl = broken ? "GUARD BROKEN" : (cap.shield != SH_HELD ? "NO SHIELD - BARE FISTS" : (cap.guarding ? "GUARDING - REFLECT" : "GUARD [RMB]"));
    drawTextCentered(b, cx, by + sh + 6 * ui, 1.6f * ui, gl, broken ? red : blue);
    std::string es = cap.shield == SH_OUT || cap.shield == SH_WAIT ? "[E] RECALL" : (cap.shield == SH_BACK ? "[E] RETURNING" :
                     (cap.eCd > 0.0f ? "[E] " + std::to_string((int)ceilf(cap.eCd)) : "[E] THROW"));
    std::string qs = cap.shield == SH_RICO || cap.shield == SH_HOME ? "[Q] BOUNCING" : (cap.qCd > 0.0f ? "[Q] " + std::to_string((int)ceilf(cap.qCd)) : "[Q] RICOCHET");
    bool eReady = cap.shield == SH_HELD && cap.eCd <= 0.0f, qReady = cap.shield == SH_HELD && cap.qCd <= 0.0f;
    drawText(b, w - textWidth(es, 2.0f * ui) - 24 * ui, 60 * ui, 2.0f * ui, es, eReady ? blue : white);
    drawText(b, w - textWidth(qs, 2.0f * ui) - 24 * ui, 32 * ui, 2.0f * ui, qs, qReady ? blue : white);
}

void renderHUD(int w, int h) {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float ui = h / 720.0f;
    float cx = w * 0.5f, cy = h * 0.5f;
    Batch2D b;
    glm::vec4 white(1, 1, 1, 0.9f), cyan(0.45f, 0.92f, 1.0f, 1.0f), orange(1.0f, 0.55f, 0.18f, 1.0f);

    if (!player.dead) {
        float g = (focusActive ? 9.0f : 6.0f) * ui, l = 6.0f * ui, t = 2.0f * ui;
        // 줄을 걸 수 있는 곳을 보고 있으면 조준점이 초록, 기차에 매달려 있으면 노랑
        glm::vec4 cc = grap.onTrain ? glm::vec4(1.0f, 0.85f, 0.2f, 1.0f)
                     : (grappleAimOK ? glm::vec4(0.45f, 1.0f, 0.5f, 1.0f) : (focusActive ? cyan : white));
        b.rect(cx - g - l, cy - t * 0.5f, l, t, cc);
        b.rect(cx + g, cy - t * 0.5f, l, t, cc);
        b.rect(cx - t * 0.5f, cy - g - l, t, l, cc);
        b.rect(cx - t * 0.5f, cy + g, t, l, cc);
        b.rect(cx - t * 0.5f, cy - t * 0.5f, t, t, cc);
    }

    if (transformItem == ITEM_CAPTAIN) {
        drawCapHUD(b, w, h);
        drawText(b, 16 * ui, h - 70 * ui, 1.6f * ui, "CAPTAIN AMERICA   [Z] REVERT", glm::vec4(0.45f, 0.65f, 1.0f, 0.95f));
    } else {
        // 집중 게이지
        float bw = 240.0f * ui, bh = 7.0f * ui, bx = cx - bw * 0.5f, by = 34.0f * ui;
        b.rect(bx - 2 * ui, by - 2 * ui, bw + 4 * ui, bh + 4 * ui, glm::vec4(0, 0, 0, 0.45f));
        b.rect(bx, by, bw * focusEnergy, bh, focusActive ? glm::vec4(1, 1, 1, 0.95f) : glm::vec4(cyan.r, cyan.g, cyan.b, 0.85f));
        drawTextCentered(b, cx, by + bh + 6 * ui, 1.6f * ui, "BULLET TIME [SHIFT]", glm::vec4(cyan.r, cyan.g, cyan.b, 0.8f));
    }

    // R 칸 (소모품): 체력 칸 위
    {
        float sx = 24 * ui, sy = 54 * ui, ss = 30 * ui;
        bool has = arrowCount > 0 || arrow.active;
        glm::vec4 rc(1.0f, 0.25f, 0.15f, 1.0f);
        b.rect(sx - 2 * ui, sy - 2 * ui, ss + 4 * ui, ss + 4 * ui, has ? rc : glm::vec4(1, 1, 1, 0.25f));
        b.rect(sx, sy, ss, ss, glm::vec4(0.05f, 0.05f, 0.07f, 0.85f));
        drawText(b, sx + 9 * ui, sy + 9 * ui, 2.0f * ui, "R", has ? rc : glm::vec4(1, 1, 1, 0.35f));
        std::string rs = arrow.active ? "YONDU ARROW " + std::to_string((int)ceilf(arrow.t)) + "S"
                       : (arrowCount > 0 ? "YONDU ARROW X" + std::to_string(arrowCount) : "EMPTY");
        drawText(b, sx + ss + 10 * ui, sy + 11 * ui, 1.5f * ui, rs, has ? rc : glm::vec4(1, 1, 1, 0.35f));
    }
    for (int i = 0; i < stats.maxHp; ++i)
        b.rect((24 + i * 22) * ui, 28 * ui, 16 * ui, 16 * ui, i < player.hp ? orange : glm::vec4(0, 0, 0, 0.4f));

    std::string ws = currentCell == HUB_CELL ? "HUB"
                   : (roomCleared[currentCell] ? "ROOM " + std::to_string(currentCell) + " CLEAR"
                                               : "WAVE " + std::to_string(wave) + "/" + std::to_string(ROOM_WAVES));
    std::string ks = "KILLS " + std::to_string(kills);
    drawText(b, w - textWidth(ws, 2.2f * ui) - 24 * ui, h - 40 * ui, 2.2f * ui, ws, white);
    drawText(b, w - textWidth(ks, 1.8f * ui) - 24 * ui, h - 66 * ui, 1.8f * ui, ks, white);
    drawText(b, 16 * ui, h - 28 * ui, 1.4f * ui, std::to_string((int)fps) + " FPS", glm::vec4(1, 1, 1, 0.5f));

    if (bannerT > 0.0f && !player.dead && !rotAnimActive)
        drawTextCentered(b, cx, h * 0.68f, 3.4f * ui, bannerText, glm::vec4(0.6f, 1.0f, 1.0f, std::min(1.0f, bannerT)));
    else if (waveBannerT > 0.0f && !player.dead && !rotAnimActive)
        drawTextCentered(b, cx, h * 0.68f, 6.0f * ui, "WAVE " + std::to_string(wave), glm::vec4(1, 1, 1, std::min(1.0f, waveBannerT)));
    if (grap.onTrain)
        drawTextCentered(b, cx, h * 0.36f, 1.8f * ui, "HOLD RMB - RIDING TO THE NEXT ROOM", glm::vec4(1.0f, 0.85f, 0.2f, 1.0f));
    if (helpT > 0.0f && transformItem == ITEM_CAPTAIN) {
        glm::vec4 hc(1, 1, 1, std::min(1.0f, helpT) * 0.85f);
        drawTextCentered(b, cx, h * 0.26f, 1.7f * ui, "LMB SHIELD / FIST   HOLD RMB GUARD + REFLECT   E THROW / RECALL", hc);
        drawTextCentered(b, cx, h * 0.26f - 20 * ui, 1.7f * ui, "Q RICOCHET   SHIFT SPRINT + LANDING SLIDE   Z REVERT", hc);
    } else if (helpT > 0.0f) {
        glm::vec4 hc(1, 1, 1, std::min(1.0f, helpT) * 0.85f);
        drawTextCentered(b, cx, h * 0.26f, 1.7f * ui, "WASD MOVE   SPACE JUMP   CTRL SPRINT   HOLD RMB WEB SWING", hc);
        drawTextCentered(b, cx, h * 0.26f - 20 * ui, 1.7f * ui, "LMB SHOOT   HOLD SHIFT BULLET TIME   R ITEM   SHIFT+R RESTART", hc);
    }
    if (player.dead) {
        drawTextCentered(b, cx, h * 0.55f, 6.0f * ui, "YOU WERE HIT", glm::vec4(1, 0.25f, 0.2f, 1));
        drawTextCentered(b, cx, h * 0.47f, 2.2f * ui, "PRESS R TO RETRY", white);
    }
    drawText(b, 16 * ui, h - 48 * ui, 1.4f * ui, "TAB CUBE", glm::vec4(1, 1, 1, 0.6f));
    if (rotAnimActive) drawTextCentered(b, cx, h * 0.68f, 3.0f * ui, "WORLD ROTATING", glm::vec4(1.0f, 0.85f, 0.2f, 1.0f));
    // 이상 현상 알림 (아이템 이름은 숨긴다)
    if (encounterCell >= 0) {
        bool here = currentCell == encounterCell;
        std::string es = here ? "SOMETHING STRANGE IS IN THIS ROOM" : "STRANGE PHENOMENON IN ROOM " + std::to_string(encounterCell) + "   [TAB]";
        float big = encounterBannerT > 0.0f ? 2.6f : 1.8f;
        drawTextCentered(b, cx, h - 34 * ui, big * ui, es, glm::vec4(1.0f, 0.35f, 0.95f, 0.75f + 0.25f * sinf(globalTime * 5.0f)));
    }
    if (nearEncounter())
        drawTextCentered(b, cx, h * 0.38f, 2.4f * ui, "[F] INVESTIGATE", glm::vec4(1.0f, 0.4f, 0.95f, 1.0f));
    if (wheelOpen) drawWheel(b, w, h);
    if (nearAugmentPickup() && !augPicking)
        drawTextCentered(b, cx, h * 0.38f, 2.4f * ui, "[F] CHOOSE AUGMENT", glm::vec4(1.0f, 0.82f, 0.35f, 1.0f));
    if (augPicking) drawAugmentPicker(b, w, h);

    flushHUD(b, w, h);
}

void renderCubeHUD(int w, int h) {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    float ui = h / 720.0f;
    Batch2D b;
    glm::vec4 white(1, 1, 1, 0.92f), dim(1, 1, 1, 0.65f), yellow(1.0f, 0.85f, 0.2f, 1.0f), cyan(0.3f, 0.95f, 1.0f, 1.0f);

    // 큐브 공간 → 프레임버퍼 픽셀 (y 위 방향)
    auto toFb = [&](glm::vec3 p) {
        glm::vec4 c = cubeVP * cubeModel() * glm::vec4(p, 1.0f);
        return glm::vec2((c.x / c.w * 0.5f + 0.5f) * w, (c.y / c.w * 0.5f + 0.5f) * h);
    };

    drawTextCentered(b, w * 0.5f, h - 44 * ui, 2.6f * ui, "CUBE", white);
    glm::vec2 cur = toFb(glm::vec3(cubies[currentCell].pos));
    drawTextCentered(b, cur.x, cur.y - 3 * ui, 1.3f * ui, "YOU", cyan);
    if (encounterCell >= 0) {
        glm::vec2 ep = toFb(glm::vec3(cubies[encounterCell].pos));
        drawTextCentered(b, ep.x, ep.y + 10 * ui, 1.3f * ui, "STRANGE", glm::vec4(1.0f, 0.35f, 0.95f, 1.0f));
    }
    glm::vec2 g = toFb(glm::vec3(0, -2.7f, 0));
    drawTextCentered(b, g.x, g.y - 6 * ui, 1.3f * ui, "GRAVITY", white);

    drawTextCentered(b, w * 0.5f, 84 * ui, 1.6f * ui, "DRAG OUTSIDE THE CUBE - ROTATE WHOLE CUBE", dim);
    drawTextCentered(b, w * 0.5f, 62 * ui, 1.6f * ui, "DRAG ON A CELL - TURN THAT SLICE (YELLOW)", yellow);
    drawTextCentered(b, w * 0.5f, 40 * ui, 1.6f * ui, "DOUBLE CLICK OR ENTER - GO TO THAT CELL", dim);
    drawTextCentered(b, w * 0.5f, 18 * ui, 1.6f * ui, "R - RESET CUBE      TAB - BACK AND APPLY ROTATION", dim);

    if (cubies[currentCell].orient != worldOrient)
        drawText(b, 20 * ui, h - 48 * ui, 1.6f * ui, "YOUR WORLD WILL ROTATE", yellow);

    // 지금까지 얻은 증강: 계열마다 이름 + 단계 칸 (가진 단계 / 전체 단계)
    float px = 20 * ui, py = h - 96 * ui;
    drawText(b, px, py, 1.9f * ui, "AUGMENTS " + std::to_string(ownedAugments.size()) + "/26", cyan);
    py -= 26 * ui;
    if (ownedAugments.empty()) drawText(b, px, py, 1.4f * ui, "CLEAR A ROOM TO PICK ONE", dim);
    for (int l = 0; l < AUG_LINE_COUNT; ++l) {
        int t = augLineTier(l);
        if (t == 0) continue;
        drawText(b, px, py, 1.4f * ui, augLineName(l), white);
        drawPips(b, px + 150 * ui, py + 1 * ui, 7 * ui, t, augLineTiers(l), cyan);
        py -= 19 * ui;
    }

    flushHUD(b, w, h);
}

void flushHUD(Batch2D& b, int w, int h) {
    // 픽셀 좌표 → NDC (3x3 동차좌표, 행 우선)
    float m[9] = { 2.0f / w, 0, -1, 0, 2.0f / h, -1, 0, 0, 1 };
    glUseProgram(hudProg);
    glUniformMatrix3fv(U(hudProg, "xform"), 1, GL_TRUE, m);
    glBindVertexArray(hudVao);
    glBindBuffer(GL_ARRAY_BUFFER, hudVbo);
    glBufferData(GL_ARRAY_BUFFER, b.tris.size() * sizeof(float), b.tris.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(b.tris.size() / 6));
    glBindVertexArray(0);
    glDisable(GL_BLEND);
}
