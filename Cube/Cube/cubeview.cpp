// 큐브 화면(Tab): 조작과 그리기
#include "cube.h"

// ===================================================================
// 큐브 화면 렌더링
// ===================================================================
GLuint cubeProg;

// ===================================================================
// 큐브 화면 (Tab): 드래그로 큐브 전체 회전 / 한 면 회전
// ===================================================================
bool cubeView = false;

glm::quat cubeViewQ(1.0f, 0.0f, 0.0f, 0.0f);
int dragMode = DRAG_NONE;
double dragStartX = 0.0, dragStartY = 0.0, dragLastX = 0.0, dragLastY = 0.0, lastClickTime = -10.0;
glm::vec3 grabP(0.0f);
int grabNormalAxis = 0, lastClickCell = -1;
glm::ivec3 grabCell(0);
int sliceAxis = -1, sliceLayer = 0;
float sliceAngle = 0.0f, sliceTarget = 0.0f;
glm::vec2 sliceScreenVel(0.0f);
int hoverCell = -1, hoverAxis = -1, hoverLayer = 0;

// 월드 회전 연출
bool rotAnimActive = false;
float rotAnimT = 0.0f;
glm::quat rotAnimFrom(1.0f, 0.0f, 0.0f, 0.0f);
glm::vec3 preTopDir(0, 1, 0);

glm::mat4 cubeModel() { return glm::mat4_cast(cubeViewQ); }

glm::mat4 cubeViewProj() {
    int w, h;
    glfwGetFramebufferSize(gWindow, &w, &h);
    return glm::perspective(glm::radians(35.0f), (float)w / (float)std::max(h, 1), 0.1f, 50.0f) *
           glm::lookAt(glm::vec3(0, 0, 9.5f), glm::vec3(0.0f), glm::vec3(0, 1, 0));
}

// 큐브 공간의 점 → 창 좌표 (마우스와 같은 y 아래 방향)
glm::vec2 cubeToWindow(glm::vec3 p) {
    int ww, wh;
    glfwGetWindowSize(gWindow, &ww, &wh);
    glm::vec4 c = cubeViewProj() * cubeModel() * glm::vec4(p, 1.0f);
    return glm::vec2((c.x / c.w * 0.5f + 0.5f) * ww, (0.5f - c.y / c.w * 0.5f) * wh);
}

// 마우스 광선과 큐브 겉면 교차. 큐브 공간의 교차점과 면 축을 돌려준다
bool pickCube(double mx, double my, glm::vec3& hit, int& normalAxis) {
    int ww, wh;
    glfwGetWindowSize(gWindow, &ww, &wh);
    if (ww <= 0 || wh <= 0) return false;
    glm::mat4 inv = glm::inverse(cubeViewProj() * cubeModel());
    float nx = (float)(2.0 * mx / ww - 1.0), ny = (float)(1.0 - 2.0 * my / wh);
    glm::vec4 a = inv * glm::vec4(nx, ny, -1, 1), b = inv * glm::vec4(nx, ny, 1, 1);
    glm::vec3 o = glm::vec3(a) / a.w, d = glm::vec3(b) / b.w - o;
    float tmin = -1e9f, tmax = 1e9f;
    int axis = -1;
    for (int i = 0; i < 3; ++i) {
        if (fabsf(d[i]) < 1e-7f) {
            if (fabsf(o[i]) > 1.5f) return false;
            continue;
        }
        float t1 = (-1.5f - o[i]) / d[i], t2 = (1.5f - o[i]) / d[i];
        if (t1 > t2) std::swap(t1, t2);
        if (t1 > tmin) { tmin = t1; axis = i; }
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return false;
    }
    if (axis < 0 || tmin < 0.0f) return false;
    hit = o + d * tmin;
    normalAxis = axis;
    return true;
}

glm::ivec3 cellFromHit(glm::vec3 p) {
    return glm::ivec3(glm::clamp(glm::floor(p + 1.5f), glm::vec3(0.0f), glm::vec3(2.0f))) - glm::ivec3(1);
}

// 커서가 칸 안에서 좌우로 치우치면 그 방향의 줄, 위아래로 치우치면 다른 방향의 줄을 강조
void updateHover(double mx, double my) {
    glm::vec3 p;
    int n;
    hoverCell = -1;
    hoverAxis = -1;
    if (!pickCube(mx, my, p, n)) return;
    glm::ivec3 c = cellFromHit(p);
    hoverCell = cubieAt(c);
    int t1 = (n + 1) % 3, t2 = (n + 2) % 3;
    hoverAxis = fabsf(p[t1] - c[t1]) >= fabsf(p[t2] - c[t2]) ? t2 : t1;
    hoverLayer = c[hoverAxis];
}

// 큐브 공간의 점이 axis 축으로 1라디안 돌 때 화면에서 움직이는 픽셀 수
glm::vec2 screenVelocity(glm::vec3 p, int axis) {
    glm::vec3 a(0.0f);
    a[axis] = 1.0f;
    return (cubeToWindow(p + glm::cross(a, p) * 0.01f) - cubeToWindow(p)) / 0.01f;
}

void cubeMouseDown(double mx, double my) {
    if (dragMode == DRAG_SNAP) return;
    dragStartX = dragLastX = mx;
    dragStartY = dragLastY = my;
    glm::vec3 p;
    int n;
    if (!pickCube(mx, my, p, n)) {
        dragMode = DRAG_CUBE;   // 큐브 바깥을 잡으면 큐브 전체 회전
        return;
    }
    grabP = p;
    grabNormalAxis = n;
    grabCell = cellFromHit(p);
    dragMode = DRAG_PENDING;
}

void cubeMouseMove(double mx, double my) {
    if (dragMode == DRAG_CUBE) {
        float dx = (float)(mx - dragLastX), dy = (float)(my - dragLastY);
        cubeViewQ = glm::normalize(glm::angleAxis(dy * 0.008f, glm::vec3(1, 0, 0)) *
                                   glm::angleAxis(dx * 0.008f, glm::vec3(0, 1, 0)) * cubeViewQ);
    } else if (dragMode == DRAG_PENDING || dragMode == DRAG_SLICE) {
        glm::vec2 total((float)(mx - dragStartX), (float)(my - dragStartY));
        if (dragMode == DRAG_PENDING && glm::length(total) > 6.0f) {
            // 끌기 시작한 방향과 가장 잘 맞는 회전축을 고른다
            int t1 = (grabNormalAxis + 1) % 3, t2 = (grabNormalAxis + 2) % 3;
            glm::vec2 s1 = screenVelocity(grabP, t1), s2 = screenVelocity(grabP, t2), dir = glm::normalize(total);
            float a1 = fabsf(glm::dot(glm::normalize(s1), dir)), a2 = fabsf(glm::dot(glm::normalize(s2), dir));
            sliceAxis = a1 >= a2 ? t1 : t2;
            sliceScreenVel = a1 >= a2 ? s1 : s2;
            sliceLayer = grabCell[sliceAxis];
            sliceAngle = 0.0f;
            dragMode = DRAG_SLICE;
        }
        if (dragMode == DRAG_SLICE) sliceAngle = glm::dot(total, sliceScreenVel) / glm::dot(sliceScreenVel, sliceScreenVel);
    }
    dragLastX = mx;
    dragLastY = my;
}

void cubeMouseUp(double mx, double my) {
    if (dragMode == DRAG_SLICE) {
        sliceTarget = glm::round(sliceAngle / (PI * 0.5f)) * (PI * 0.5f);   // 가장 가까운 90도로 맞춤
        dragMode = DRAG_SNAP;
        return;
    }
    if (dragMode == DRAG_PENDING) {
        // 움직이지 않고 두 번 누르면 그 칸으로 이동
        int cell = cubieAt(grabCell);
        double now = glfwGetTime();
        bool twice = now - lastClickTime < 0.35 && cell == lastClickCell;
        lastClickTime = now;
        lastClickCell = cell;
        dragMode = DRAG_NONE;
        if (twice) enterCell(cell);
        return;
    }
    dragMode = DRAG_NONE;
}

void updateCubeView(float dt) {
    if (dragMode == DRAG_SNAP) {
        float diff = sliceTarget - sliceAngle, step = dt * 6.0f;
        if (fabsf(diff) <= step) {
            int turns = (int)glm::round(sliceTarget / (PI * 0.5f));
            if (turns != 0) turnSlice(sliceAxis, sliceLayer, turns);
            sliceAxis = -1;
            sliceAngle = 0.0f;
            dragMode = DRAG_NONE;
        } else {
            sliceAngle += diff > 0.0f ? step : -step;
        }
    }
    if (dragMode == DRAG_NONE) {
        double mx, my;
        glfwGetCursorPos(gWindow, &mx, &my);
        updateHover(mx, my);
    }
}

void openCubeView() {
    releaseGrapple();
    cubeView = true;
    dragMode = DRAG_NONE;
    sliceAxis = -1;
    glfwSetInputMode(gWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

// 현재 칸의 방향이 바뀌었으면 월드 전체를 그만큼 회전시킨다
void applyCubeToWorld() {
    glm::mat3 target = cubies[currentCell].orient;
    glm::mat3 r = target * glm::transpose(worldOrient);
    if (r == glm::mat3(1.0f)) return;
    worldOrient = target;
    rebuildWorld();
    placeAugmentPickup();   // 방이 돌면 바닥이 바뀌므로 증강 물체도 새 바닥의 빈 자리로

    auto rot = [&](glm::vec3 p) { return ROOM_C + r * (p - ROOM_C); };
    // 눈 위치를 기준으로 옮겨서 회전 연출이 끊기지 않게 한다
    glm::vec3 eye = rot(eyePosition());
    player.pos = eye - glm::vec3(0, EYE_H, 0);
    player.pos.x = glm::clamp(player.pos.x, -ROOM_HALF + 0.5f, ROOM_HALF - 0.5f);
    player.pos.z = glm::clamp(player.pos.z, -ROOM_HALF + 0.5f, ROOM_HALF - 0.5f);
    player.pos.y = glm::clamp(player.pos.y, 0.0f, ROOM_HALF * 2.0f - 2.5f);
    player.vel = glm::vec3(0.0f);
    player.onGround = false;
    for (auto& e : enemies) {
        e.pos = rot(e.pos);
        e.pos.x = glm::clamp(e.pos.x, -ROOM_HALF + 0.5f, ROOM_HALF - 0.5f);
        e.pos.z = glm::clamp(e.pos.z, -ROOM_HALF + 0.5f, ROOM_HALF - 0.5f);
        e.vy = 0.0f;
    }
    for (auto& b : bullets) { b.pos = rot(b.pos); b.vel = r * b.vel; }
    for (auto& s : shards) { s.pos = rot(s.pos); s.vel = r * s.vel; s.axis = r * s.axis; }
    for (auto& s : sparks) { s.pos = rot(s.pos); s.vel = r * s.vel; }

    rotAnimActive = true;
    rotAnimT = 0.0f;
    rotAnimFrom = glm::quat_cast(glm::transpose(r));
    preTopDir = r * glm::vec3(0, 1, 0);
}

void leaveCubeInput() {
    cubeView = false;
    dragMode = DRAG_NONE;
    sliceAxis = -1;
    glfwSetInputMode(gWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

void closeCubeView() {
    leaveCubeInput();
    applyCubeToWorld();
}

void enterCell(int id) {
    if (id == currentCell) { closeCubeView(); return; }
    leaveCubeInput();
    travelTo(id, glm::vec3(0.0f));     // 큐브 화면에서 바로 이동 (개발용)
}


glm::mat4 cubeVP(1.0f);

void drawCubePart(const glm::mat4& m, glm::vec3 color, float emissive) {
    glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(m)));
    glm::mat4 mvp = cubeVP * m;
    glUniformMatrix4fv(U(cubeProg, "mvp"), 1, GL_FALSE, glm::value_ptr(mvp));
    glUniformMatrix3fv(U(cubeProg, "normalMat"), 1, GL_FALSE, glm::value_ptr(nm));
    glUniform3fv(U(cubeProg, "color"), 1, glm::value_ptr(color));
    glUniform1f(U(cubeProg, "emissive"), emissive);
    glBindVertexArray(cubeMesh.vao);
    glDrawArrays(GL_TRIANGLES, 0, cubeMesh.count);
}

// 상자의 모서리 12개를 얇은 막대로 그림 (선 굵기가 드라이버마다 달라서 도형으로 처리)
void drawBoxEdges(const glm::mat4& m, glm::vec3 c, glm::vec3 half, float thick, glm::vec3 color) {
    for (int a = 0; a < 3; ++a) {
        int b = (a + 1) % 3, d = (a + 2) % 3;
        for (int sb = -1; sb <= 1; sb += 2)
            for (int sd = -1; sd <= 1; sd += 2) {
                glm::vec3 p = c, s(thick);
                p[b] += half[b] * sb;
                p[d] += half[d] * sd;
                s[a] = half[a] * 2.0f + thick;
                drawCubePart(m * TRS(p, s), color, 1.0f);
            }
    }
}

void renderCubeView(int w, int h) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    glClearColor(0.05f, 0.06f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glUseProgram(cubeProg);
    cubeVP = cubeViewProj();
    glm::mat4 cm = cubeModel();

    auto sliceSpin = [&]() {
        glm::vec3 a(0.0f);
        a[sliceAxis] = 1.0f;
        return glm::rotate(glm::mat4(1.0f), sliceAngle, a);
    };

    for (int i = 0; i < 27; ++i) {
        const Cubie& c = cubies[i];
        glm::mat4 m = cm;
        if (sliceAxis >= 0 && c.pos[sliceAxis] == sliceLayer) m = m * sliceSpin();
        m = glm::translate(m, glm::vec3(c.pos)) * glm::mat4(c.orient);
        drawCubePart(glm::scale(m, glm::vec3(0.97f)), glm::vec3(0.05f, 0.05f, 0.06f), 0.0f);
        float glow = 0.0f;
        if (i == currentCell) glow = 0.2f + 0.2f * sinf(globalTime * 5.0f);
        else if (i == hoverCell && dragMode == DRAG_NONE) glow = 0.2f;
        else if (roomCleared[i]) glow = 0.1f;   // 깬 방은 살짝 밝게
        // 스티커 색 = 그 방의 벽 색. 윗면 스티커 색을 보면 어느 벽이 하늘 쪽인지 알 수 있다
        for (int f = 0; f < 6; ++f)
            drawCubePart(m * TRS(FACE_DIRS[f] * 0.485f, glm::vec3(0.85f) - glm::abs(FACE_DIRS[f]) * 0.82f), FACE_COLORS[f], glow);
    }

    // 강조선은 칸 뒤에 가려지지 않게 깊이 검사를 끄고 그린다
    glDisable(GL_DEPTH_TEST);
    int ax = sliceAxis >= 0 ? sliceAxis : hoverAxis;
    int layer = sliceAxis >= 0 ? sliceLayer : hoverLayer;
    if (ax >= 0) {
        glm::vec3 c(0.0f), half(1.52f);
        c[ax] = (float)layer;
        half[ax] = 0.52f;
        drawBoxEdges(sliceAxis >= 0 ? cm * sliceSpin() : cm, c, half, 0.045f, glm::vec3(1.0f, 0.85f, 0.2f));
    }
    glm::mat4 curM = cm;
    if (sliceAxis >= 0 && cubies[currentCell].pos[sliceAxis] == sliceLayer) curM = curM * sliceSpin();
    drawBoxEdges(curM, glm::vec3(cubies[currentCell].pos), glm::vec3(0.5f), 0.03f, glm::vec3(0.3f, 0.95f, 1.0f));

    // 중력 방향(큐브 공간의 -Y)은 큐브와 함께 돌며 항상 아래쪽 칸을 가리킨다
    drawCubePart(cm * TRS(glm::vec3(0, -2.05f, 0), glm::vec3(0.04f, 0.8f, 0.04f)), glm::vec3(1.0f), 0.5f);
    drawCubePart(cm * TRS(glm::vec3(0, -2.47f, 0), glm::vec3(0.16f, 0.1f, 0.16f)), glm::vec3(1.0f), 0.5f);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
}

