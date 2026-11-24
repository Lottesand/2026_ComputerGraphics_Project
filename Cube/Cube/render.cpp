// 렌더링 파이프라인: 그림자, HDR, 인스턴싱, 반투명, 블룸, 합성
#include "cube.h"

// ===================================================================
// 렌더링
// ===================================================================
GLuint sceneProg, shadowProg, skyProg, brightProg, blurProg, compositeProg, hudProg;

GLuint emptyVao, hudVao, hudVbo;
SceneLocs sl;
long long statDrawCalls = 0, statTriangles = 0;    // 성능 측정용 (프레임마다 0으로)
GLint shadowModelLoc, shadowVPLoc;

GLuint shadowFbo, shadowTex;
Targets rt;

bool shadowPass = false;

GLuint makeTex(int w, int h, GLenum internalFmt, GLenum fmt, GLenum type) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, w, h, 0, fmt, type, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

void createShadowMap() {
    glGenTextures(1, &shadowTex);
    glBindTexture(GL_TEXTURE_2D, shadowTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, SHADOW_SIZE, SHADOW_SIZE, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float border[4] = { 1, 1, 1, 1 };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glGenFramebuffers(1, &shadowFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) std::cerr << "그림자 FBO 생성 실패\n";
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void createTargets(int w, int h) {
    if (rt.hdrFbo) {
        glDeleteFramebuffers(1, &rt.hdrFbo);
        glDeleteTextures(1, &rt.hdrColor);
        glDeleteTextures(1, &rt.hdrNormal);
        glDeleteRenderbuffers(1, &rt.hdrDepth);
        glDeleteFramebuffers(2, rt.bloomFbo);
        glDeleteTextures(2, rt.bloomTex);
    }
    rt.w = w;
    rt.h = h;

    rt.hdrColor = makeTex(w, h, GL_RGBA16F, GL_RGBA, GL_FLOAT);
    rt.hdrNormal = makeTex(w, h, GL_RGBA16F, GL_RGBA, GL_FLOAT);
    glGenRenderbuffers(1, &rt.hdrDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, rt.hdrDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glGenFramebuffers(1, &rt.hdrFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, rt.hdrFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt.hdrColor, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, rt.hdrNormal, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rt.hdrDepth);
    GLenum bufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, bufs);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) std::cerr << "HDR FBO 생성 실패\n";

    glGenFramebuffers(2, rt.bloomFbo);
    for (int i = 0; i < 2; ++i) {
        rt.bloomTex[i] = makeTex(std::max(1, w / 2), std::max(1, h / 2), GL_RGBA16F, GL_RGBA, GL_FLOAT);
        glBindFramebuffer(GL_FRAMEBUFFER, rt.bloomFbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt.bloomTex[i], 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
Batch batches[2];           // 0 = 상자, 1 = 사면체 파편
GLuint instVbo = 0;
bool batching = false;
GLint sceneInstancedLoc = -1, shadowInstancedLoc = -1;

void createInstancing() {
    glGenBuffers(1, &instVbo);
    const Mesh* meshes[2] = { &cubeMesh, &tetraMesh };
    for (int b = 0; b < 2; ++b) {
        batches[b].mesh = meshes[b];
        glGenVertexArrays(1, &batches[b].vao);
        glBindVertexArray(batches[b].vao);
        glBindBuffer(GL_ARRAY_BUFFER, meshes[b]->vbo);
        for (int a = 0; a < 3; ++a) {
            glVertexAttribPointer(a, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(a * 3 * sizeof(float)));
            glEnableVertexAttribArray(a);
        }
        glBindBuffer(GL_ARRAY_BUFFER, instVbo);
        const GLsizei stride = sizeof(Inst);
        for (int c = 0; c < 4; ++c) {   // mat4 = vec4 속성 4개
            glVertexAttribPointer(3 + c, 4, GL_FLOAT, GL_FALSE, stride, (void*)(offsetof(Inst, m) + c * sizeof(glm::vec4)));
            glEnableVertexAttribArray(3 + c);
            glVertexAttribDivisor(3 + c, 1);
        }
        glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Inst, colorEmis));
        glVertexAttribPointer(8, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Inst, mat));
        glVertexAttribPointer(9, 1, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Inst, pattern));
        for (int a = 7; a <= 9; ++a) {
            glEnableVertexAttribArray(a);
            glVertexAttribDivisor(a, 1);
        }
        glBindVertexArray(0);
    }
}

// 모아 둔 인스턴스를 메쉬마다 한 번의 glDrawArraysInstanced 로 그린다
void flushBatches() {
    GLint loc = shadowPass ? shadowInstancedLoc : sceneInstancedLoc;
    glUniform1i(loc, 1);
    for (auto& b : batches) {
        if (b.items.empty()) continue;
        glBindBuffer(GL_ARRAY_BUFFER, instVbo);
        glBufferData(GL_ARRAY_BUFFER, b.items.size() * sizeof(Inst), nullptr, GL_STREAM_DRAW);   // 이전 프레임 버퍼를 버리고 새로 받음
        glBufferSubData(GL_ARRAY_BUFFER, 0, b.items.size() * sizeof(Inst), b.items.data());
        glBindVertexArray(b.vao);
        glDrawArraysInstanced(GL_TRIANGLES, 0, b.mesh->count, (GLsizei)b.items.size());
        ++statDrawCalls;
        statTriangles += (long long)b.mesh->count / 3 * (long long)b.items.size();
        b.items.clear();    // 용량은 남겨 두어 다음 프레임에 다시 할당하지 않는다
    }
    glBindVertexArray(0);
    glUniform1i(loc, 0);
}

void drawMesh(const Mesh& mesh, const glm::mat4& m, const Material& mat) {
    if (batching) {
        int bi = &mesh == &cubeMesh ? 0 : (&mesh == &tetraMesh ? 1 : -1);
        if (bi >= 0) {
            batches[bi].items.push_back({ m, glm::vec4(mat.color, mat.emissive),
                                          glm::vec4(mat.ks, mat.shininess, (float)mat.tex, mat.texScale), (float)mat.pattern });
            return;
        }
    }
    if (shadowPass) {
        glUniformMatrix4fv(shadowModelLoc, 1, GL_FALSE, glm::value_ptr(m));
    } else {
        glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(m)));
        glUniformMatrix4fv(sl.model, 1, GL_FALSE, glm::value_ptr(m));
        glUniformMatrix3fv(sl.normalMat, 1, GL_FALSE, glm::value_ptr(nm));
        glUniform3fv(sl.objColor, 1, glm::value_ptr(mat.color));
        glUniform1f(sl.Ks, mat.ks);
        glUniform1f(sl.shininess, mat.shininess);
        glUniform1f(sl.emissive, mat.emissive);
        glUniform1i(sl.pattern, mat.pattern);
        glUniform1i(sl.texLayer, mat.tex);
        glUniform1f(sl.texScale, mat.texScale);
        glUniform3f(sl.objScale, glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2])));
    }
    glBindVertexArray(mesh.vao);
    glDrawArrays(GL_TRIANGLES, 0, mesh.count);
    ++statDrawCalls;
    statTriangles += mesh.count / 3;
}

glm::mat4 TRS(glm::vec3 t, glm::vec3 s) {
    return glm::scale(glm::translate(glm::mat4(1.0f), t), s);
}

// 방향 벡터를 +Z 축으로 하는 회전 행렬
glm::mat4 orientTo(glm::vec3 dir) {
    glm::vec3 z = glm::normalize(dir);
    glm::vec3 up = fabsf(z.y) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    glm::vec3 x = glm::normalize(glm::cross(up, z));
    glm::vec3 y = glm::cross(z, x);
    return glm::mat4(glm::vec4(x, 0), glm::vec4(y, 0), glm::vec4(z, 0), glm::vec4(0, 0, 0, 1));
}

void drawEnemy(const Enemy& e) {
    EnemyRig r = enemyRig(e);
    Material skin = e.rusher ? M_RUSHER : M_ENEMY;
    if (e.spawnT < 1.0f) skin.emissive = (1.0f - e.spawnT) * 2.0f;
    for (int i = 0; i < 2; ++i)
        drawMesh(cubeMesh, r.legs[i] * TRS({ 0, -0.45f, 0 }, { 0.2f, 0.9f, 0.22f }), M_ENEMY_DARK);
    drawMesh(cubeMesh, r.torso * glm::scale(glm::mat4(1.0f), glm::vec3(0.55f, 0.75f, 0.3f)), skin);
    drawMesh(cubeMesh, r.head * glm::scale(glm::mat4(1.0f), glm::vec3(0.3f, 0.34f, 0.3f)), skin);
    drawMesh(cubeMesh, r.head * TRS({ 0, 0.02f, 0.155f }, { 0.24f, 0.06f, 0.02f }), e.rusher ? M_RUSHER_VISOR : M_VISOR);
    for (int i = 0; i < 2; ++i)
        drawMesh(cubeMesh, r.arms[i] * TRS({ 0, -0.35f, 0 }, { 0.14f, 0.7f, 0.14f }), skin);
    if (!e.rusher) drawMesh(cubeMesh, r.arms[1] * TRS({ 0, -0.75f, 0.04f }, { 0.08f, 0.34f, 0.12f }), M_DARK);   // 총 (근접형은 맨손)
}

void drawPart(const glm::mat4& m, const Material& mat) {
    if (shadowPass && mat.emissive > 0.0f) return;   // 빛나는 부품은 그림자를 만들지 않는다
    drawMesh(cubeMesh, m, mat);
}

// 객차: 선로 좌표계(trackFrame)에 방 회전(roomToWorld)을 곱한 행렬 하나로 몸체와 부품을 모두 배치
void drawTrains() {
    Material body{ glm::vec3(0.62f, 0.64f, 0.68f), 0.9f, 72.0f, 0.0f, 0, TEX_METAL, 7.0f };
    Material dark{ glm::vec3(0.12f, 0.13f, 0.15f), 0.5f, 32.0f, 0.0f, 0 };
    Material win{ glm::vec3(1.0f, 0.8f, 0.52f), 0.0f, 1.0f, 1.1f, 0 };
    Material red{ glm::vec3(1.0f, 0.12f, 0.08f), 0.0f, 1.0f, 6.0f, 0 };
    for (int k = 0; k < (int)tracks.size(); ++k) {
        const Track& t = tracks[k];
        for (int i = 0; i < t.cars; ++i) {
            if (!carVisible(t, carS(t, i))) continue;
            glm::mat4 C = carWorld(k, i);
            drawPart(C * glm::scale(glm::mat4(1.0f), glm::vec3(CAR_LEN, CAR_H, CAR_W)), body);
            drawPart(C * TRS({ 0, -CAR_H * 0.5f + 0.3f, 0 }, { CAR_LEN * 0.99f, 0.6f, CAR_W + 0.03f }), dark);
            drawPart(C * TRS({ 0, CAR_H * 0.5f + 0.06f, 0 }, { CAR_LEN * 0.95f, 0.12f, CAR_W * 0.7f }), dark);
            drawPart(C * TRS({ 0, 0.35f, 0 }, { CAR_LEN * 0.9f, 0.75f, CAR_W + 0.04f }), win);
            // 양옆 출입문 두 개씩 + 선두·후미 행선판
            for (int d = -1; d <= 1; d += 2)
                drawPart(C * TRS({ d * CAR_LEN * 0.25f, -0.15f, 0 }, { 1.2f, 2.3f, CAR_W + 0.06f }), dark);
            if (i == 0) drawPart(C * TRS({ CAR_LEN * 0.5f + 0.02f, 0.9f, 0 }, { 0.06f, 0.4f, 1.6f }), win);
            for (int s = -1; s <= 1; s += 2) {
                if (i == 0) drawPart(C * TRS({ CAR_LEN * 0.5f + 0.03f, -0.55f, s * 0.8f }, { 0.06f, 0.32f, 0.42f }), red);
                if (i == t.cars - 1) drawPart(C * TRS({ -CAR_LEN * 0.5f - 0.03f, -0.55f, s * 0.8f }, { 0.06f, 0.32f, 0.42f }), red);
            }
        }
    }
}

// 포털: 각 면 중앙. 그 방향에 칸이 있을 때만 생기고, 방을 깨면 열린다(청록), 아니면 잠김(빨강)
void drawPortals(bool films) {
    if (!portalsOn()) return;
    bool open = roomCleared[currentCell];
    for (const auto& D : FACE_DIRS) {
        if (portalTarget(D) < 0) continue;
        int n = fabsf(D.x) > 0.5f ? 0 : (fabsf(D.y) > 0.5f ? 1 : 2);
        int a = (n + 1) % 3, b = (n + 2) % 3;
        glm::vec3 ad = glm::abs(D), c = ROOM_C + D * ROOM_HALF;
        if (!films) {
            Material fm{ open ? glm::vec3(0.3f, 0.95f, 1.0f) : glm::vec3(0.9f, 0.2f, 0.15f), 0.0f, 1.0f, open ? 3.0f : 1.2f, 0 };
            for (int pass = 0; pass < 2; ++pass) {
                int along = pass == 0 ? a : b, across = pass == 0 ? b : a;
                for (int s = -1; s <= 1; s += 2) {
                    glm::vec3 p = c - D * 0.15f, size = ad * 0.3f;
                    p[across] += s * 4.2f;
                    size[along] = 8.9f;
                    size[across] = 0.5f;
                    drawPart(TRS(p, size), fm);
                }
            }
        } else if (open) {
            Material film{ glm::vec3(0.3f, 0.9f, 1.0f), 0.0f, 1.0f, 1.2f + 0.4f * sinf(globalTime * 3.0f), 0 };
            drawMesh(cubeMesh, TRS(c - D * 0.1f, (glm::vec3(1.0f) - ad) * 8.0f + ad * 0.05f), film);
        }
    }
}

// 캡틴 아메리카 방패: 바깥부터 빨강, 흰색, 빨강 고리, 파란 원, 흰 별. 지역 +Z 가 앞면, 반지름 1
void drawCapShield(const glm::mat4& M) {
    Material red{ glm::vec3(0.72f, 0.06f, 0.08f), 0.7f, 56.0f, 0.0f, 0 };
    Material white{ glm::vec3(0.92f, 0.92f, 0.95f), 0.7f, 56.0f, 0.0f, 0 };
    Material blue{ glm::vec3(0.06f, 0.14f, 0.52f), 0.7f, 56.0f, 0.0f, 0 };
    Material steel{ glm::vec3(0.62f, 0.64f, 0.68f), 0.9f, 72.0f, 0.0f, 0 };
    Material strap{ glm::vec3(0.28f, 0.16f, 0.08f), 0.1f, 8.0f, 0.0f, 0 };
    auto disk = [&](float r, float z, const Material& m) {
        drawMesh(diskMesh, M * glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, z)) * glm::scale(glm::mat4(1.0f), glm::vec3(r, r, 1.0f)), m);
    };
    // 뒷면(금속, -Z 를 향함)과 손잡이 끈 두 줄
    drawMesh(diskMesh, M * glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, -0.03f)) * glm::rotate(glm::mat4(1.0f), PI, glm::vec3(0, 1, 0)) *
                       glm::scale(glm::mat4(1.0f), glm::vec3(1.02f, 1.02f, 1.0f)), steel);
    for (int s = -1; s <= 1; s += 2) drawMesh(cubeMesh, M * TRS({ 0, s * 0.16f, -0.07f }, { 0.75f, 0.09f, 0.06f }), strap);
    disk(1.0f, 0.0f, red);
    disk(0.8f, 0.006f, white);
    disk(0.6f, 0.012f, red);
    disk(0.4f, 0.018f, blue);
    drawMesh(starMesh, M * glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, 0.024f)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.38f, 0.38f, 1.0f)), white);
}

// 이상 현상(변신 아이템): 검은 핵이 돌고 자홍색 조각이 맴돌며, 멀리서도 보이게 빛기둥
void drawEncounterItem() {
    if (!encounterPlaced) return;
    glm::vec3 c = encounterPos + glm::vec3(0, 1.5f + 0.2f * sinf(globalTime * 2.5f), 0);
    Material core{ glm::vec3(0.05f, 0.0f, 0.08f), 1.0f, 90.0f, 0.4f, 0 };
    Material glow{ glm::vec3(1.0f, 0.2f, 0.9f), 0.0f, 1.0f, 3.5f + 1.5f * sinf(globalTime * 7.0f), 0 };
    Material beam{ glm::vec3(0.8f, 0.2f, 1.0f), 0.0f, 1.0f, 1.6f, 0 };
    glm::mat4 R = glm::rotate(glm::mat4(1.0f), globalTime * 2.2f, glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f)));
    drawPart(glm::translate(glm::mat4(1.0f), c) * R * glm::scale(glm::mat4(1.0f), glm::vec3(0.7f)), core);
    drawPart(glm::translate(glm::mat4(1.0f), c) * glm::inverse(R) * glm::scale(glm::mat4(1.0f), glm::vec3(0.45f)), glow);
    for (int k = 0; k < 7; ++k) {
        float a = globalTime * (1.5f + k * 0.2f) + k * 0.9f, rr = 1.0f + 0.15f * k;
        glm::vec3 p = c + glm::vec3(cosf(a) * rr, sinf(a * 1.7f) * 0.6f, sinf(a) * rr);
        if (!shadowPass) drawMesh(tetraMesh, glm::translate(glm::mat4(1.0f), p) * glm::rotate(glm::mat4(1.0f), a * 3.0f, glm::vec3(1, 1, 0)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.22f)), glow);
    }
    drawPart(TRS(encounterPos + glm::vec3(0, 20.0f, 0), glm::vec3(0.12f, 40.0f, 0.12f)), beam);
}

// 방을 깨면 나오는 증강 물체: 받침대 + 떠서 도는 금빛 결정 + 주위를 도는 작은 조각 4개
void drawAugmentPickup() {
    if (!augPickupActive) return;
    glm::vec3 p = augPickupPos;
    Material base{ glm::vec3(0.16f, 0.17f, 0.2f), 0.5f, 32.0f, 0.0f, 0 };
    Material gold{ glm::vec3(1.0f, 0.78f, 0.3f), 0.0f, 1.0f, 3.5f + 1.0f * sinf(globalTime * 4.0f), 0 };
    Material trim{ glm::vec3(1.0f, 0.78f, 0.3f), 0.0f, 1.0f, 2.0f, 0 };
    drawPart(TRS(p + glm::vec3(0, 0.4f, 0), glm::vec3(1.3f, 0.8f, 1.3f)), base);
    drawPart(TRS(p + glm::vec3(0, 0.82f, 0), glm::vec3(1.34f, 0.05f, 1.34f)), trim);
    glm::vec3 c = p + glm::vec3(0, 1.75f + 0.15f * sinf(globalTime * 2.0f), 0);
    glm::mat4 M = glm::translate(glm::mat4(1.0f), c) * glm::rotate(glm::mat4(1.0f), globalTime * 1.6f, glm::vec3(0, 1, 0)) *
                  glm::rotate(glm::mat4(1.0f), 0.785f, glm::vec3(1, 0, 1));
    drawPart(M * glm::scale(glm::mat4(1.0f), glm::vec3(0.55f)), gold);
    for (int k = 0; k < 4; ++k) {
        float a = globalTime * 2.4f + k * PI * 0.5f;
        drawPart(TRS(c + glm::vec3(cosf(a) * 0.95f, 0.25f * sinf(a * 2.0f), sinf(a) * 0.95f), glm::vec3(0.12f)), trim);
    }
}

void drawWorld(bool withEffects) {
    for (const auto& b : boxes) {
        if (shadowPass && b.mat.emissive > 0.0f) continue;
        if (b.glassy) continue;     // 반투명은 drawTransparent 에서
        if (b.shellDir != glm::vec3(0.0f)) {
            // 외벽은 그림자를 드리우지 않고, 맨 위에 온 면은 열어서 하늘이 보이게 한다 (허브는 유리 지붕)
            if (shadowPass || (b.shellDir.y > 0.5f && currentCell != HUB_CELL)) continue;
            if (rotAnimActive && glm::dot(b.shellDir, preTopDir) > 0.5f && currentCell != HUB_CELL) continue;
        }
        drawMesh(cubeMesh, TRS(b.center, b.size), b.mat);
    }
    glm::mat4 W = roomToWorld();
    for (const auto& d : localDecor) drawPart(W * d.model, d.mat);
    drawTrains();
    drawHoles();
    drawAugmentPickup();
    drawEncounterItem();
    // 던진 방패: 원반처럼 눕혀서 돈다
    if (transformItem == ITEM_CAPTAIN && cap.shield != SH_HELD)
        drawCapShield(glm::translate(glm::mat4(1.0f), cap.sPos) * orientTo(glm::vec3(0, 1, 0)) *
                      glm::rotate(glm::mat4(1.0f), cap.spin, glm::vec3(0, 0, 1)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.75f)));
    if (!shadowPass) drawPortals(false);
    if (withEffects && grap.active) {
        glm::vec3 eye = eyePosition(), fwd = forwardDir();
        glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
        glm::vec3 hand = eye + right * 0.25f - glm::vec3(0, 0.35f, 0) + fwd * 0.4f;
        glm::vec3 d = grap.anchor - hand;
        float L = glm::length(d);
        Material rope{ glm::vec3(0.75f, 0.95f, 1.0f), 0.0f, 1.0f, 2.5f, 0 };
        if (L > 0.05f) drawMesh(cubeMesh, glm::translate(glm::mat4(1.0f), hand + d * 0.5f) * orientTo(d) * glm::scale(glm::mat4(1.0f), glm::vec3(0.035f, 0.035f, L)), rope);
    }
    for (const auto& e : enemies) drawEnemy(e);
    for (const auto& s : shards) {
        float fade = std::min(1.0f, s.life);
        glm::mat4 m = glm::translate(glm::mat4(1.0f), s.pos) * glm::rotate(glm::mat4(1.0f), s.angle, s.axis);
        Material mat = s.glow ? M_VISOR : M_ENEMY;
        mat.color = s.color;
        drawMesh(tetraMesh, m * glm::scale(glm::mat4(1.0f), glm::vec3(s.size * fade)), mat);
    }
    if (!withEffects) return;

    for (const auto& b : bullets) {
        glm::vec3 dir = glm::normalize(b.vel);
        glm::mat4 o = orientTo(dir);
        Material core{ b.fromPlayer ? glm::vec3(0.7f, 0.95f, 1.0f) : glm::vec3(1.0f, 0.45f, 0.12f), 0.0f, 1.0f, 6.0f, 0 };
        Material trail = core;
        trail.emissive = 1.6f;
        float len = b.fromPlayer ? 0.9f : 0.45f, thick = b.fromPlayer ? 0.035f : 0.07f;
        drawMesh(cubeMesh, glm::translate(glm::mat4(1.0f), b.pos - dir * len * 0.5f) * o * glm::scale(glm::mat4(1.0f), glm::vec3(thick, thick, len)), core);
        float tl = b.fromPlayer ? 3.0f : 2.2f;
        drawMesh(cubeMesh, glm::translate(glm::mat4(1.0f), b.pos - dir * (len + tl * 0.5f)) * o * glm::scale(glm::mat4(1.0f), glm::vec3(thick * 0.45f, thick * 0.45f, tl)), trail);
    }
    for (const auto& s : sparks) {
        Material m{ s.color, 0.0f, 1.0f, 4.0f, 0 };
        drawMesh(cubeMesh, TRS(s.pos, glm::vec3(0.05f * std::min(1.0f, s.life * 3.0f))), m);
    }
}
int statLights = 0;
void uploadLights(glm::vec3 eye) {
    std::vector<LightSrc> cand;
    cand.reserve(localLights.size() + 40);
    glm::mat4 W = roomToWorld();
    for (const auto& l : localLights) cand.push_back({ glm::vec3(W * glm::vec4(l.pos, 1.0f)), l.col, l.range });
    for (int k = 0; k < (int)tracks.size(); ++k)
        for (int i = 0; i < tracks[k].cars; ++i)
            if (carVisible(tracks[k], carS(tracks[k], i)))
                cand.push_back({ glm::vec3(carWorld(k, i)[3]), glm::vec3(1.0f, 0.72f, 0.42f) * 1.6f, 10.0f });
    if (encounterPlaced) cand.push_back({ encounterPos + glm::vec3(0, 1.6f, 0), glm::vec3(1.0f, 0.25f, 0.9f) * 3.0f, 11.0f });
    if (augPickupActive) cand.push_back({ augPickupPos + glm::vec3(0, 1.8f, 0), glm::vec3(1.0f, 0.78f, 0.35f) * 3.0f, 9.0f });
    int n = std::min((int)cand.size(), MAX_LIGHTS);
    std::partial_sort(cand.begin(), cand.begin() + n, cand.end(), [&](const LightSrc& a, const LightSrc& b) {
        return glm::length(a.pos - eye) - a.range < glm::length(b.pos - eye) - b.range;
    });
    std::vector<glm::vec3> pos(n), col(n);
    std::vector<float> range(n);
    for (int i = 0; i < n; ++i) { pos[i] = cand[i].pos; col[i] = cand[i].col; range[i] = cand[i].range; }
    glUniform1i(sl.numLights, n);
    if (n > 0) {
        glUniform3fv(sl.lightPos, n, glm::value_ptr(pos[0]));
        glUniform3fv(sl.lightCol, n, glm::value_ptr(col[0]));
        glUniform1fv(sl.lightRange, n, range.data());
    }
    statLights = n;
}

// 반투명: 불투명한 것을 다 그린 뒤, 깊이는 쓰지 않고 카메라에서 먼 것부터(뒤→앞) 알파 블렌딩
void drawTransparent(glm::vec3 eye) {
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    glColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);   // 외곽선용 법선 버퍼는 건드리지 않는다

    std::vector<const Box*> glassList;
    for (const auto& b : boxes) if (b.glassy) glassList.push_back(&b);
    std::sort(glassList.begin(), glassList.end(), [&](const Box* a, const Box* b) {
        return glm::length(a->center - eye) > glm::length(b->center - eye);
    });
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUniform1f(sl.alpha, 0.28f);
    for (const Box* b : glassList) drawMesh(cubeMesh, TRS(b->center, b->size), b->mat);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE);      // 포털 막은 빛이므로 더하기
    glUniform1f(sl.alpha, 0.35f + 0.1f * sinf(globalTime * 2.0f));
    drawPortals(true);

    glUniform1f(sl.alpha, 1.0f);
    glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

// 캡틴 1인칭: 오른손 방패 + 왼손 주먹. 근접은 번갈아 앞으로 내지르고, 가드는 방패를 화면 가운데로 세운다
static void drawCapViewModel(const glm::mat4& invView, float bx, float by) {
    Material glove{ glm::vec3(0.42f, 0.16f, 0.1f), 0.2f, 16.0f, 0.0f, 0 };
    Material sleeve{ glm::vec3(0.08f, 0.14f, 0.42f), 0.3f, 24.0f, 0.0f, 0 };
    float p = cap.meleeT > 0.0f ? 1.0f - cap.meleeT / 0.22f : 0.0f, punch = sinf(p * PI);
    bool held = cap.shield == SH_HELD;
    auto fist = [&](float side, float lp) {
        glm::mat4 F = invView * glm::translate(glm::mat4(1.0f), glm::vec3(side * (0.27f - 0.12f * lp) + bx, -0.3f - by + 0.05f * lp, -0.55f - 0.4f * lp));
        drawMesh(cubeMesh, F * glm::scale(glm::mat4(1.0f), glm::vec3(0.1f, 0.1f, 0.13f)), glove);
        drawMesh(cubeMesh, F * TRS({ 0, 0, 0.17f }, { 0.11f, 0.11f, 0.22f }), sleeve);
    };
    fist(-1.0f, cap.meleeSide == 1 ? punch : 0.0f);
    if (!held) { fist(1.0f, 0.0f); return; }
    float sp = cap.meleeSide == 0 ? punch : 0.0f;
    glm::mat4 S;
    if (cap.guarding)
        S = invView * glm::translate(glm::mat4(1.0f), glm::vec3(0.02f, -0.12f, -0.5f)) * glm::rotate(glm::mat4(1.0f), PI, glm::vec3(0, 1, 0)) *
            glm::scale(glm::mat4(1.0f), glm::vec3(0.26f));
    else
        S = invView * glm::translate(glm::mat4(1.0f), glm::vec3(0.34f - 0.14f * sp + bx, -0.3f - by + 0.06f * sp, -0.66f - 0.35f * sp)) *
            glm::rotate(glm::mat4(1.0f), -0.6f + 0.5f * sp, glm::vec3(0, 1, 0)) * glm::rotate(glm::mat4(1.0f), 0.15f, glm::vec3(1, 0, 0)) *
            glm::scale(glm::mat4(1.0f), glm::vec3(0.2f));
    drawCapShield(S);
}

void drawViewModel(const glm::mat4& view) {
    glm::mat4 invView = glm::inverse(view);
    float bx = player.onGround ? cosf(player.bobPhase) * 0.012f : 0.0f;
    float by = player.onGround ? fabsf(sinf(player.bobPhase)) * 0.012f : 0.0f;
    if (transformItem == ITEM_CAPTAIN && !player.dead) { drawCapViewModel(invView, bx, by); return; }
    float lower = player.dead ? std::min(1.0f, player.deadT) * 0.5f : 0.0f;
    glm::mat4 gun = invView * glm::translate(glm::mat4(1.0f), glm::vec3(0.21f + bx, -0.19f - by - lower, -0.5f + player.recoil * 0.05f));
    gun = glm::rotate(gun, player.recoil * 0.3f, glm::vec3(1, 0, 0));
    gun = glm::scale(gun, glm::vec3(0.75f));

    Material body{ glm::vec3(0.1f, 0.1f, 0.12f), 0.6f, 48.0f, 0.0f, 0 };
    Material glove{ glm::vec3(0.22f, 0.22f, 0.24f), 0.1f, 8.0f, 0.0f, 0 };
    Material stripe{ glm::vec3(1.0f, 0.5f, 0.15f), 0.0f, 1.0f, 1.2f, 0 };
    drawMesh(cubeMesh, gun * TRS({ 0, 0.02f, -0.1f }, { 0.06f, 0.07f, 0.3f }), body);
    drawMesh(cubeMesh, gun * TRS({ 0.031f, 0.025f, -0.1f }, { 0.004f, 0.012f, 0.22f }), stripe);
    glm::mat4 grip = glm::rotate(glm::translate(gun, glm::vec3(0, -0.08f, 0.02f)), 0.25f, glm::vec3(1, 0, 0));
    drawMesh(cubeMesh, grip * glm::scale(glm::mat4(1.0f), glm::vec3(0.055f, 0.14f, 0.08f)), body);
    drawMesh(cubeMesh, gun * TRS({ 0, -0.1f, 0.05f }, { 0.11f, 0.09f, 0.12f }), glove);
    if (player.muzzleT > 0.0f) {
        Material flash{ glm::vec3(1.0f, 0.8f, 0.45f), 0.0f, 1.0f, 8.0f, 0 };
        glm::mat4 m = glm::rotate(glm::translate(gun, glm::vec3(0, 0.02f, -0.29f)), frand(0, PI), glm::vec3(0, 0, 1));
        drawMesh(cubeMesh, m * glm::scale(glm::mat4(1.0f), glm::vec3(0.12f, 0.02f, 0.02f)), flash);
        drawMesh(cubeMesh, m * glm::scale(glm::mat4(1.0f), glm::vec3(0.02f, 0.12f, 0.02f)), flash);
    }
}

void drawFullscreen() {
    glBindVertexArray(emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void renderFrame(int w, int h) {
    if (w != rt.w || h != rt.h) createTargets(w, h);

    glm::vec3 eye = eyePosition(), fwd = forwardDir();
    glm::vec3 up(0, 1, 0);
    if (!player.dead && fabsf(player.camRoll) > 1e-4f) {
        glm::vec3 rightH(cosf(player.yaw), 0, sinf(player.yaw));
        up = glm::normalize(up * cosf(player.camRoll) + rightH * sinf(player.camRoll));
    }
    if (player.dead) {
        float roll = std::min(1.0f, player.deadT) * 0.5f;
        glm::vec3 right = glm::normalize(glm::cross(fwd, up));
        up = glm::normalize(up * cosf(roll) + right * sinf(roll));
    }
    // 월드 회전 연출: 월드는 이미 회전을 마친 상태이고, 남은 회전 A 를 시점에 곱해 되감아 보여준다
    glm::mat4 rollA(1.0f);
    if (rotAnimActive) {
        float t = glm::smoothstep(0.0f, 1.0f, glm::clamp(rotAnimT, 0.0f, 1.0f));
        glm::quat q = glm::slerp(rotAnimFrom, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), t);
        rollA = glm::translate(glm::mat4(1.0f), ROOM_C) * glm::mat4_cast(q) * glm::translate(glm::mat4(1.0f), -ROOM_C);
    }
    glm::vec3 skyEye = glm::vec3(rollA * glm::vec4(eye, 1.0f));
    glm::mat4 skyView = glm::lookAt(skyEye, skyEye + fwd, up);
    glm::mat4 view = skyView * rollA;
    float fov = 75.0f - (1.0f - timeScale) * 6.0f + player.camFov;
    glm::mat4 proj = glm::perspective(glm::radians(fov), (float)w / (float)h, 0.03f, FAR_PLANE);

    // 해는 큐브 공간에 고정 → 방이 돌면 빛이 들어오는 방향도 달라진다
    glm::vec3 sunW = glm::transpose(glm::mat3(rollA)) * SUN_DIR;
    // 그림자 범위도 방 크기에 비례 (방 대각선을 덮도록)
    const float sx = 42.0f * ROOM_SCALE;
    glm::mat4 lightView = glm::lookAt(ROOM_C + sunW * (90.0f * ROOM_SCALE), ROOM_C, fabsf(sunW.y) > 0.95f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0));
    glm::mat4 lightVP = glm::ortho(-sx, sx, -sx, sx, 10.0f, 170.0f * ROOM_SCALE) * lightView;

    // 1) 그림자 패스
    shadowPass = true;
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo);
    glViewport(0, 0, SHADOW_SIZE, SHADOW_SIZE);
    glEnable(GL_DEPTH_TEST);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);
    glUseProgram(shadowProg);
    glUniform1i(shadowInstancedLoc, 0);
    glUniformMatrix4fv(shadowVPLoc, 1, GL_FALSE, glm::value_ptr(lightVP));
    batching = true;
    drawWorld(false);
    flushBatches();
    batching = false;
    glDisable(GL_POLYGON_OFFSET_FILL);
    shadowPass = false;

    // 2) 씬 패스 (HDR, MRT)
    glBindFramebuffer(GL_FRAMEBUFFER, rt.hdrFbo);
    glViewport(0, 0, w, h);
    float clearC[4] = { 0, 0, 0, 1 }, clearN[4] = { 0.5f, 0.5f, 1.0f, 1.0f };
    glClearBufferfv(GL_COLOR, 0, clearC);
    glClearBufferfv(GL_COLOR, 1, clearN);
    glClear(GL_DEPTH_BUFFER_BIT);

    glDisable(GL_DEPTH_TEST);
    glUseProgram(skyProg);
    glm::mat4 invVP = glm::inverse(proj * skyView);
    glUniformMatrix4fv(U(skyProg, "invViewProj"), 1, GL_FALSE, glm::value_ptr(invVP));
    glUniform3fv(U(skyProg, "camPos"), 1, glm::value_ptr(skyEye));
    glUniform3fv(U(skyProg, "sunDir"), 1, glm::value_ptr(SUN_DIR));
    glUniform3fv(U(skyProg, "zenith"), 1, glm::value_ptr(SKY_ZENITH));
    glUniform3fv(U(skyProg, "horizon"), 1, glm::value_ptr(SKY_HORIZON));
    glUniform3fv(U(skyProg, "sunColor"), 1, glm::value_ptr(SUN_COLOR));
    drawFullscreen();
    glEnable(GL_DEPTH_TEST);

    glUseProgram(sceneProg);
    glUniform1i(sceneInstancedLoc, 0);
    glUniformMatrix4fv(sl.view, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(sl.proj, 1, GL_FALSE, glm::value_ptr(proj));
    glUniformMatrix4fv(sl.lightVP, 1, GL_FALSE, glm::value_ptr(lightVP));
    // 허브(유리 지붕 기차역)는 차갑고 어두운 회청색 조명
    bool hub = currentCell == HUB_CELL;
    glm::vec3 sunC = hub ? glm::vec3(0.75f, 0.8f, 0.9f) : SUN_COLOR;
    glm::vec3 fogC = hub ? glm::vec3(0.2f, 0.24f, 0.3f) : SKY_HORIZON;
    glUniform3fv(sl.sunDir, 1, glm::value_ptr(sunW));
    glUniform3fv(sl.sunColor, 1, glm::value_ptr(sunC));
    if (hub) glUniform3f(sl.skyAmb, 0.2f, 0.25f, 0.33f);
    else glUniform3f(sl.skyAmb, 0.32f, 0.4f, 0.58f);
    if (hub) glUniform3f(sl.groundAmb, 0.1f, 0.11f, 0.13f);
    else glUniform3f(sl.groundAmb, 0.32f, 0.24f, 0.2f);
    glUniform3fv(sl.viewPos, 1, glm::value_ptr(eye));
    glUniform3fv(sl.fogColor, 1, glm::value_ptr(fogC));
    glUniform1f(sl.fogDensity, hub ? 0.009f : 0.012f);
    glUniform1f(sl.farPlane, FAR_PLANE);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, shadowTex);
    glUniform1i(sl.shadowMap, 0);
    glm::vec3 muzzleWorld = eye + fwd * 0.8f;
    glUniform3fv(sl.pointPos, 1, glm::value_ptr(muzzleWorld));
    glUniform3f(sl.pointColor, 1.0f, 0.75f, 0.4f);
    glUniform1f(sl.pointPower, player.muzzleT > 0.0f ? 6.0f : 0.0f);

    glUniform1f(sl.alpha, 1.0f);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D_ARRAY, albedoArray);
    glUniform1i(sl.albedoTex, 1);
    glActiveTexture(GL_TEXTURE0);
    uploadLights(eye);
    glUniform1i(sl.style, styleForCell(currentCell));
    batching = true;
    drawWorld(true);
    flushBatches();
    batching = false;
    drawTransparent(eye);

    glClear(GL_DEPTH_BUFFER_BIT);
    drawViewModel(view);

    // 3) 블룸
    glDisable(GL_DEPTH_TEST);
    int bw = std::max(1, w / 2), bh = std::max(1, h / 2);
    glViewport(0, 0, bw, bh);
    glBindFramebuffer(GL_FRAMEBUFFER, rt.bloomFbo[0]);
    glUseProgram(brightProg);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, rt.hdrColor);
    glUniform1i(U(brightProg, "src"), 0);
    drawFullscreen();

    glUseProgram(blurProg);
    glUniform1i(U(blurProg, "src"), 0);
    glUniform2f(U(blurProg, "texel"), 1.0f / bw, 1.0f / bh);
    for (int i = 0; i < 8; ++i) {
        int horiz = i % 2 == 0 ? 1 : 0;
        glBindFramebuffer(GL_FRAMEBUFFER, rt.bloomFbo[horiz]);
        glBindTexture(GL_TEXTURE_2D, rt.bloomTex[1 - horiz]);
        glUniform1i(U(blurProg, "horizontal"), horiz);
        drawFullscreen();
    }

    // 4) 합성
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    glUseProgram(compositeProg);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, rt.hdrColor);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, rt.hdrNormal);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, rt.bloomTex[0]);
    glUniform1i(U(compositeProg, "sceneTex"), 0);
    glUniform1i(U(compositeProg, "normalTex"), 1);
    glUniform1i(U(compositeProg, "bloomTex"), 2);
    glUniform2f(U(compositeProg, "texel"), 1.0f / w, 1.0f / h);
    glUniform1f(U(compositeProg, "focus"), glm::clamp((1.0f - timeScale) / 0.9f, 0.0f, 1.0f));
    glUniform1f(U(compositeProg, "hurt"), std::max(player.hurtT, player.dead ? 0.6f : 0.0f));
    glUniform1f(U(compositeProg, "time"), globalTime);
    glUniform1i(U(compositeProg, "style"), styleForCell(currentCell));
    glUniform3fv(U(compositeProg, "fadeColor"), 1, glm::value_ptr(fadeColor));
    glUniform1f(U(compositeProg, "fade"), fadeT * fadeT);
    drawFullscreen();
    glActiveTexture(GL_TEXTURE0);
}

// ===================================================================
// 방마다 다른 렌더 스타일: 허브는 실사, 데모 경기장은 원래 화면, 나머지 방은 칸 번호로 돌아가며
// ===================================================================
int styleForCell(int cell) {
    if (cell == HUB_CELL) return STYLE_REAL;
    if (cell == ARENA_CELL) return STYLE_MINIMAL;
    static const int cycle[5] = { STYLE_TOON, STYLE_PIXEL, STYLE_NOIR, STYLE_NEON, STYLE_BLUEPRINT };
    return cycle[cell % 5];
}

const char* styleName(int style) {
    static const char* names[STYLE_COUNT] = { "REAL", "MINIMAL", "TOON", "PIXEL", "NOIR", "NEON", "BLUEPRINT" };
    return style >= 0 && style < STYLE_COUNT ? names[style] : "?";
}

// 그 스타일 방으로 가는 구멍 속 빛의 색
glm::vec3 styleTint(int style) {
    static const glm::vec3 tints[STYLE_COUNT] = {
        { 0.75f, 0.86f, 1.0f }, { 1.0f, 0.8f, 0.55f }, { 1.0f, 0.55f, 0.2f }, { 0.45f, 1.0f, 0.4f },
        { 0.92f, 0.92f, 0.92f }, { 1.0f, 0.2f, 0.9f }, { 0.3f, 0.6f, 1.0f },
    };
    return tints[glm::clamp(style, 0, STYLE_COUNT - 1)];
}

// ===================================================================
// 벽의 구멍: 검은 테두리 + 안쪽으로 갈수록 밝아지는 동심원 (터널 속 빛). 기차가 이 안으로 들어간다
// ===================================================================
void drawHoleAt(glm::vec3 c, glm::vec3 inward, float r, glm::vec3 color) {
    if (r < 0.05f || shadowPass) return;
    glm::mat4 O = glm::translate(glm::mat4(1.0f), c) * orientTo(inward);
    Material rim{ glm::vec3(0.02f, 0.02f, 0.03f), 0.0f, 1.0f, 0.0f, 0 };
    drawMesh(diskMesh, O * glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, 0.03f)) * glm::scale(glm::mat4(1.0f), glm::vec3(r * 1.12f, r * 1.12f, 1.0f)), rim);
    for (int k = 0; k < 4; ++k) {
        float pulse = 0.05f * sinf(globalTime * 6.0f - k * 1.3f);
        float rr = r * (1.0f - k * 0.23f + pulse);
        Material m{ glm::mix(color, glm::vec3(1.0f), k * 0.22f), 0.0f, 1.0f, 1.2f + k * 1.1f, 0 };
        drawMesh(diskMesh, O * glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, 0.05f + 0.01f * k)) * glm::scale(glm::mat4(1.0f), glm::vec3(rr, rr, 1.0f)), m);
    }
}

void drawHoles() {
    if (shadowPass) return;
    glm::mat4 W = roomToWorld();
    for (const Track& t : tracks)
        for (int side = 0; side < 2; ++side) {
            bool exitSide = side == 0;
            float o = holeOpen(t, exitSide);
            if (o <= 0.0f) continue;
            glm::vec3 n = trackWallNormal(t, exitSide);
            glm::vec3 p = trackWallPoint(t, exitSide) + glm::vec3(0, CAR_H * 0.5f - 1.2f, 0);
            glm::vec3 D = worldOrient * glm::round(t.dir) * (exitSide ? 1.0f : -1.0f);
            int other = cubieAt(cubies[currentCell].pos + glm::ivec3(glm::round(D)));
            glm::vec3 color = other >= 0 ? styleTint(styleForCell(other)) : glm::vec3(1.0f);
            drawHoleAt(glm::vec3(W * glm::vec4(p, 1.0f)), worldOrient * -n, 6.5f * glm::smoothstep(0.0f, 1.0f, o), color);
        }
    if (arrivalHoleT > 0.0f)
        drawHoleAt(arrivalHolePos, arrivalHoleNormal, 6.5f * glm::smoothstep(0.0f, 1.0f, arrivalHoleT), arrivalHoleColor);
}
