// 공통 유틸: 난수, 셰이더 로드, 메쉬 생성
#include "cube.h"

std::mt19937 rng(777);
Mesh diskMesh, sphereMesh;

float frand(float a, float b) { std::uniform_real_distribution<float> d(a, b); return d(rng); }

std::string filetobuf(const char* file) {
    std::ifstream shaderFile(file);
    if (!shaderFile.is_open()) {
        std::string altPath = std::string("Cube/") + file;
        shaderFile.open(altPath);
        if (!shaderFile.is_open()) return "";
    }
    return std::string((std::istreambuf_iterator<char>(shaderFile)), std::istreambuf_iterator<char>());
}

GLuint compileShader(GLenum type, const std::string& src, const char* name) {
    const char* s = src.c_str();
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &s, NULL);
    glCompileShader(sh);
    GLint ok;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(sh, 1024, NULL, log);
        std::cerr << "[셰이더 컴파일 오류] " << name << "\n" << log << std::endl;
    }
    return sh;
}

GLuint makeProgram(const char* vsFile, const char* fsFile) {
    std::string vs = filetobuf(vsFile), fs = filetobuf(fsFile);
    if (vs.empty() || fs.empty()) {
        std::cerr << "셰이더 파일을 찾을 수 없습니다: " << vsFile << ", " << fsFile << std::endl;
        return 0;
    }
    GLuint v = compileShader(GL_VERTEX_SHADER, vs, vsFile);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs, fsFile);
    GLuint program = glCreateProgram();
    glAttachShader(program, v);
    glAttachShader(program, f);
    glLinkProgram(program);
    GLint ok;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(program, 1024, NULL, log);
        std::cerr << "[셰이더 링크 오류] " << vsFile << " + " << fsFile << "\n" << log << std::endl;
        return 0;
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return program;
}

GLint U(GLuint p, const char* name) { return glGetUniformLocation(p, name); }

Mesh cubeMesh, tetraMesh;

void pushVert(std::vector<float>& v, glm::vec3 p, glm::vec3 n) {
    v.insert(v.end(), { p.x, p.y, p.z, n.x, n.y, n.z, 1.0f, 1.0f, 1.0f });
}

void pushTri(std::vector<float>& v, glm::vec3 a, glm::vec3 b, glm::vec3 c) {
    glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
    pushVert(v, a, n); pushVert(v, b, n); pushVert(v, c, n);
}

void pushQuad(std::vector<float>& v, glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3) {
    pushTri(v, p0, p1, p2);
    pushTri(v, p0, p2, p3);
}

Mesh uploadMesh(const std::vector<float>& data) {
    Mesh m;
    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
    m.count = (GLsizei)(data.size() / 9);
    return m;
}

Mesh buildCube() {
    float s = 0.5f;
    glm::vec3 v000(-s, -s, -s), v100(s, -s, -s), v110(s, s, -s), v010(-s, s, -s);
    glm::vec3 v001(-s, -s, s), v101(s, -s, s), v111(s, s, s), v011(-s, s, s);
    std::vector<float> d;
    pushQuad(d, v001, v101, v111, v011);
    pushQuad(d, v101, v100, v110, v111);
    pushQuad(d, v100, v000, v010, v110);
    pushQuad(d, v000, v001, v011, v010);
    pushQuad(d, v011, v111, v110, v010);
    pushQuad(d, v000, v100, v101, v001);
    return uploadMesh(d);
}

// 파편용 사면체
Mesh buildTetra() {
    glm::vec3 a(0.5f, 0.5f, 0.5f), b(-0.5f, -0.5f, 0.5f), c(-0.5f, 0.5f, -0.5f), e(0.5f, -0.5f, -0.5f);
    std::vector<float> d;
    pushTri(d, a, b, e);
    pushTri(d, a, c, b);
    pushTri(d, a, e, c);
    pushTri(d, b, c, e);
    return uploadMesh(d);
}

// 구멍(포털) 그리기용 원판: 반지름 1, +Z 를 향함
// 반지름 1 구. 정점 법선 = 위치라 매끈하게 보인다
Mesh buildSphere(int slices, int stacks) {
    std::vector<float> d;
    auto P = [&](int i, int j) {
        float th = PI * j / stacks, ph = 2.0f * PI * i / slices;
        return glm::vec3(sinf(th) * cosf(ph), cosf(th), sinf(th) * sinf(ph));
    };
    for (int j = 0; j < stacks; ++j)
        for (int i = 0; i < slices; ++i) {
            glm::vec3 a = P(i, j), b = P(i + 1, j), c = P(i + 1, j + 1), e = P(i, j + 1);
            for (glm::vec3 p : { a, c, b, a, e, c }) pushVert(d, p, p);
        }
    return uploadMesh(d);
}

Mesh buildDisk(int segments) {
    std::vector<float> d;
    for (int i = 0; i < segments; ++i) {
        float a0 = 2.0f * PI * i / segments, a1 = 2.0f * PI * (i + 1) / segments;
        pushTri(d, glm::vec3(0.0f), glm::vec3(cosf(a0), sinf(a0), 0.0f), glm::vec3(cosf(a1), sinf(a1), 0.0f));
    }
    return uploadMesh(d);
}
