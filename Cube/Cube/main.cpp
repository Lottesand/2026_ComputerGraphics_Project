// 입력 콜백과 메인 루프
#include "cube.h"

// ===================================================================
// 입력 / main
// ===================================================================
double lastMouseX = 0.0, lastMouseY = 0.0;

bool firstMouse = true;

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_Q) { glfwSetWindowShouldClose(window, true); return; }
    if (cubeView) {
        bool idle = dragMode == DRAG_NONE;
        if ((key == GLFW_KEY_TAB || key == GLFW_KEY_ESCAPE) && idle) { closeCubeView(); firstMouse = true; }
        else if (key == GLFW_KEY_ENTER && idle && hoverCell >= 0) { enterCell(hoverCell); firstMouse = true; }
        else if (key == GLFW_KEY_R && idle) initCube();
        return;
    }
    if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(window, true);
    else if (key == GLFW_KEY_R) restartRun();
    else if (key == GLFW_KEY_TAB && !rotAnimActive) openCubeView();
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (!cubeView || button != GLFW_MOUSE_BUTTON_LEFT) return;
    double mx, my;
    glfwGetCursorPos(window, &mx, &my);
    if (action == GLFW_PRESS) {
        cubeMouseDown(mx, my);
    } else if (action == GLFW_RELEASE) {
        cubeMouseUp(mx, my);
        if (!cubeView) firstMouse = true;   // 더블클릭으로 칸에 들어간 경우
    }
}

void cursor_callback(GLFWwindow* window, double x, double y) {
    if (cubeView) {
        cubeMouseMove(x, y);
        return;
    }
    if (firstMouse) { lastMouseX = x; lastMouseY = y; firstMouse = false; }
    double dx = x - lastMouseX, dy = y - lastMouseY;
    lastMouseX = x;
    lastMouseY = y;
    if (player.dead || fabs(dx) > 300.0 || fabs(dy) > 300.0) return;   // 커서 캡처 직후 튐 무시
    player.yaw += (float)dx * 0.0022f;
    player.pitch = glm::clamp(player.pitch - (float)dy * 0.0022f, -1.45f, 1.45f);
}

int main(int argc, char** argv) {
    bool shotMode = argc > 1 && std::string(argv[1]) == "--shot";
    bool benchMode = argc > 1 && std::string(argv[1]) == "--bench";
    int benchW = argc > 3 ? atoi(argv[2]) : 1280, benchH = argc > 3 ? atoi(argv[3]) : 720;
    int frameCount = 0;
    if (!glfwInit()) return -1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(benchMode ? benchW : 1280, benchMode ? benchH : 720, "Cube", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return -1;
    }
    gWindow = window;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(benchMode ? 0 : 1);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, cursor_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (glfwRawMouseMotionSupported()) glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

    sceneProg = makeProgram("scene_vertex.glsl", "scene_fragment.glsl");
    shadowProg = makeProgram("shadow_vertex.glsl", "shadow_fragment.glsl");
    skyProg = makeProgram("post_vertex.glsl", "sky_fragment.glsl");
    brightProg = makeProgram("post_vertex.glsl", "bright_fragment.glsl");
    blurProg = makeProgram("post_vertex.glsl", "blur_fragment.glsl");
    compositeProg = makeProgram("post_vertex.glsl", "composite_fragment.glsl");
    hudProg = makeProgram("hud_vertex.glsl", "hud_fragment.glsl");
    cubeProg = makeProgram("cube_vertex.glsl", "cube_fragment.glsl");
    if (!sceneProg || !shadowProg || !skyProg || !brightProg || !blurProg || !compositeProg || !hudProg || !cubeProg) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    sl.model = U(sceneProg, "model");
    sl.view = U(sceneProg, "view");
    sl.proj = U(sceneProg, "proj");
    sl.lightVP = U(sceneProg, "lightVP");
    sl.normalMat = U(sceneProg, "normalMat");
    sl.objColor = U(sceneProg, "objColor");
    sl.Ks = U(sceneProg, "Ks");
    sl.shininess = U(sceneProg, "shininess");
    sl.emissive = U(sceneProg, "emissive");
    sl.pattern = U(sceneProg, "pattern");
    sl.sunDir = U(sceneProg, "sunDir");
    sl.sunColor = U(sceneProg, "sunColor");
    sl.skyAmb = U(sceneProg, "skyAmb");
    sl.groundAmb = U(sceneProg, "groundAmb");
    sl.viewPos = U(sceneProg, "viewPos");
    sl.fogColor = U(sceneProg, "fogColor");
    sl.fogDensity = U(sceneProg, "fogDensity");
    sl.farPlane = U(sceneProg, "farPlane");
    sl.shadowMap = U(sceneProg, "shadowMap");
    sl.pointPos = U(sceneProg, "pointPos");
    sl.pointColor = U(sceneProg, "pointColor");
    sl.pointPower = U(sceneProg, "pointPower");
    sl.alpha = U(sceneProg, "alpha");
    sl.objScale = U(sceneProg, "objScale");
    sl.texLayer = U(sceneProg, "texLayer");
    sl.texScale = U(sceneProg, "texScale");
    sl.albedoTex = U(sceneProg, "albedoTex");
    sl.numLights = U(sceneProg, "numLights");
    sl.lightPos = U(sceneProg, "lightPos");
    sl.lightCol = U(sceneProg, "lightCol");
    sl.lightRange = U(sceneProg, "lightRange");
    sl.style = U(sceneProg, "style");
    shadowModelLoc = U(shadowProg, "model");
    shadowVPLoc = U(shadowProg, "lightVP");
    sceneInstancedLoc = U(sceneProg, "instanced");
    shadowInstancedLoc = U(shadowProg, "instanced");

    cubeMesh = buildCube();
    tetraMesh = buildTetra();
    createInstancing();
    diskMesh = buildDisk(48);
    glGenVertexArrays(1, &emptyVao);
    glGenVertexArrays(1, &hudVao);
    glGenBuffers(1, &hudVbo);
    glBindVertexArray(hudVao);
    glBindBuffer(GL_ARRAY_BUFFER, hudVbo);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    createShadowMap();
    double texT0 = glfwGetTime();
    createTextures();
    std::cout << "[info] textures " << TEX_COUNT << " x " << TEX_SIZE << "^2 generated in " << (int)((glfwGetTime() - texT0) * 1000) << " ms\n"
              << "[info] GPU " << (const char*)glGetString(GL_RENDERER) << " / GL " << (const char*)glGetString(GL_VERSION) << "\n";
    cubeViewQ = glm::angleAxis(0.5f, glm::vec3(1, 0, 0)) * glm::angleAxis(-0.6f, glm::vec3(0, 1, 0));
    restartRun();

    std::cout << "=== Cube ===\n"
              << "WASD 이동, Space 점프, Ctrl 달리기, 마우스 시점\n"
              << "우클릭 누르고 있기 = 줄 걸기(웹스윙). 달리는 기차에 걸면 매달려 다음 방으로\n"
              << "좌클릭 발사, Shift 누르고 있기 = 불릿타임(시간 감속)\n"
              << "방을 깨면 벽 중앙의 포털이 열린다. 큐브를 돌리면 포털 너머의 방이 바뀐다\n"
              << "R 처음부터, ESC/Q 종료\n"
              << "Tab 큐브 화면: 바깥 드래그 = 큐브 전체 회전, 칸 위 드래그 = 한 면 회전\n"
              << "               더블클릭/Enter = 그 칸으로 이동, R = 큐브 초기화, Tab = 돌아가며 회전 적용\n";

    double lastTime = glfwGetTime();
    float fpsAcc = 0.0f;
    int fpsFrames = 0;
    while (!glfwWindowShouldClose(window)) {
        double curTime = glfwGetTime();
        float dt = (float)(curTime - lastTime);
        lastTime = curTime;
        if (benchMode && !benchStep(dt * 1000.0f)) break;
        statDrawCalls = statTriangles = 0;
        fpsAcc += dt;
        ++fpsFrames;
        if (fpsAcc >= 0.5f) { fps = fpsFrames / fpsAcc; fpsAcc = 0.0f; fpsFrames = 0; }
        if (dt > 0.05f) dt = 0.05f;

        update(dt);

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        if (w > 0 && h > 0) {
            if (cubeView) {
                renderCubeView(w, h);
                renderCubeHUD(w, h);
            } else {
                renderFrame(w, h);
                renderHUD(w, h);
            }
            if (shotMode) shotScript(++frameCount, w, h);
            if (benchMode) glFinish();     // GPU 가 끝날 때까지 기다려 실제 프레임 시간을 잰다
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
