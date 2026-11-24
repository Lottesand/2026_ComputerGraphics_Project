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

    // 집중 게이지
    float bw = 240.0f * ui, bh = 7.0f * ui, bx = cx - bw * 0.5f, by = 34.0f * ui;
    b.rect(bx - 2 * ui, by - 2 * ui, bw + 4 * ui, bh + 4 * ui, glm::vec4(0, 0, 0, 0.45f));
    b.rect(bx, by, bw * focusEnergy, bh, focusActive ? glm::vec4(1, 1, 1, 0.95f) : glm::vec4(cyan.r, cyan.g, cyan.b, 0.85f));
    drawTextCentered(b, cx, by + bh + 6 * ui, 1.6f * ui, "BULLET TIME [SHIFT]", glm::vec4(cyan.r, cyan.g, cyan.b, 0.8f));

    for (int i = 0; i < 3; ++i)
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
    if (helpT > 0.0f) {
        glm::vec4 hc(1, 1, 1, std::min(1.0f, helpT) * 0.85f);
        drawTextCentered(b, cx, h * 0.26f, 1.7f * ui, "WASD MOVE   SPACE JUMP   CTRL SPRINT   HOLD RMB WEB SWING", hc);
        drawTextCentered(b, cx, h * 0.26f - 20 * ui, 1.7f * ui, "LMB SHOOT   HOLD SHIFT BULLET TIME   R RESTART", hc);
    }
    if (player.dead) {
        drawTextCentered(b, cx, h * 0.55f, 6.0f * ui, "YOU WERE HIT", glm::vec4(1, 0.25f, 0.2f, 1));
        drawTextCentered(b, cx, h * 0.47f, 2.2f * ui, "PRESS R TO RETRY", white);
    }
    drawText(b, 16 * ui, h - 48 * ui, 1.4f * ui, "TAB CUBE", glm::vec4(1, 1, 1, 0.6f));
    if (rotAnimActive) drawTextCentered(b, cx, h * 0.68f, 3.0f * ui, "WORLD ROTATING", glm::vec4(1.0f, 0.85f, 0.2f, 1.0f));

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
    glm::vec2 g = toFb(glm::vec3(0, -2.7f, 0));
    drawTextCentered(b, g.x, g.y - 6 * ui, 1.3f * ui, "GRAVITY", white);

    drawTextCentered(b, w * 0.5f, 84 * ui, 1.6f * ui, "DRAG OUTSIDE THE CUBE - ROTATE WHOLE CUBE", dim);
    drawTextCentered(b, w * 0.5f, 62 * ui, 1.6f * ui, "DRAG ON A CELL - TURN THAT SLICE (YELLOW)", yellow);
    drawTextCentered(b, w * 0.5f, 40 * ui, 1.6f * ui, "DOUBLE CLICK OR ENTER - GO TO THAT CELL", dim);
    drawTextCentered(b, w * 0.5f, 18 * ui, 1.6f * ui, "R - RESET CUBE      TAB - BACK AND APPLY ROTATION", dim);

    if (cubies[currentCell].orient != worldOrient)
        drawText(b, 20 * ui, h - 48 * ui, 1.6f * ui, "YOUR WORLD WILL ROTATE", yellow);

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
