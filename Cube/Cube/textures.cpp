// 코드로 그리는 텍스처 (텍스처 배열 + 밉맵)
#include "cube.h"

GLuint albedoArray = 0;

float thash(int x, int y, int seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xFFFFFF) / 16777215.0f;
}

// 타일링되는 값 노이즈 (period 칸마다 반복 → 텍스처 경계가 이어진다)
float tnoise(float x, float y, int period, int seed) {
    int x0 = (int)floorf(x), y0 = (int)floorf(y);
    float fx = x - x0, fy = y - y0;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    auto h = [&](int a, int b) { return thash(((a % period) + period) % period, ((b % period) + period) % period, seed); };
    float a = h(x0, y0) + (h(x0 + 1, y0) - h(x0, y0)) * fx;
    float b = h(x0, y0 + 1) + (h(x0 + 1, y0 + 1) - h(x0, y0 + 1)) * fx;
    return a + (b - a) * fy;
}

float tfbm(float u, float v, int baseFreq, int seed) {
    float s = 0.0f, amp = 0.5f;
    for (int o = 0; o < 5; ++o) {
        int f = baseFreq << o;
        s += amp * tnoise(u * f, v * f, f, seed + o * 17);
        amp *= 0.5f;
    }
    return s;
}

// 격자선까지의 거리 (0~0.5). 홈·줄눈을 그릴 때 쓴다
float gridDist(float u, int cells) { float f = u * cells; return fabsf(f - floorf(f) - 0.5f); }

glm::vec4 texel(int layer, float u, float v) {
    float n = tfbm(u, v, 8, layer * 101);
    float fine = tfbm(u, v, 64, layer * 131 + 7);
    switch (layer) {
    case TEX_CONCRETE: {
        // 거푸집 판 자국 + 폼타이 구멍 + 세로 얼룩
        float seam = std::min(gridDist(u, 2), gridDist(v, 4));
        float g = 0.78f + 0.22f * n + 0.08f * fine;
        if (seam > 0.485f) g *= 0.72f;
        float hx = u * 4.0f - floorf(u * 4.0f) - 0.5f, hy = v * 8.0f - floorf(v * 8.0f) - 0.5f;
        if (hx * hx + hy * hy < 0.004f) g *= 0.45f;
        g *= 1.0f - 0.12f * tnoise(u * 32.0f, v * 2.0f, 32, 99) * tnoise(u * 32.0f, v * 2.0f, 32, 98);
        return glm::vec4(g, g, g * 0.98f, 0.15f);
    }
    case TEX_TILE: {
        // 바닥 타일 4x4, 줄눈, 타일마다 다른 밝기와 닳은 자국
        float d = std::max(gridDist(u, 4), gridDist(v, 4));
        int tx = (int)(u * 4), ty = (int)(v * 4);
        float t = 0.86f + 0.12f * thash(tx, ty, 5) + 0.06f * fine - 0.1f * std::max(0.0f, n - 0.55f);
        if (d > 0.47f) return glm::vec4(0.32f, 0.32f, 0.33f, 0.05f);
        return glm::vec4(t, t, t * 1.01f, 0.75f);
    }
    case TEX_GLASS: {
        // 판유리 2x2 + 창틀. 위로 갈수록 밝은 반사, 유리마다 다른 색조, 먼지
        float d = std::max(gridDist(u, 2), gridDist(v, 2));
        if (d > 0.465f) return glm::vec4(0.2f, 0.21f, 0.24f, 0.4f);
        int px = (int)(u * 2), py = (int)(v * 2);
        float fv = v * 2 - py;
        float refl = 0.7f + 0.35f * fv + 0.15f * thash(px, py, 3) + 0.08f * n;
        float streak = 0.08f * tnoise(u * 40.0f, v * 3.0f, 40, 55);
        return glm::vec4(refl * 0.92f - streak, refl * 0.97f - streak, refl * 1.05f - streak, 1.0f);
    }
    case TEX_METAL: {
        // 열차 외판: 가로 헤어라인 + 세로 패널 이음매 + 리벳
        float brushed = 0.85f + 0.1f * tnoise(u * 4.0f, v * 160.0f, 4, 31) + 0.05f * n;
        float seam = gridDist(u, 4);
        if (seam > 0.49f) brushed *= 0.55f;
        float rx = u * 16.0f - floorf(u * 16.0f) - 0.5f, ry = v * 2.0f - floorf(v * 2.0f) - 0.5f;
        if (fabsf(ry) > 0.44f && rx * rx < 0.01f) brushed *= 1.15f;
        return glm::vec4(brushed, brushed * 1.01f, brushed * 1.04f, 0.95f);
    }
    case TEX_PANEL: {
        // 방 벽: 크고 작은 금속 패널, 테두리 홈, 모서리 볼트
        float big = std::max(gridDist(u, 2), gridDist(v, 2));
        float small = std::max(gridDist(u, 4), gridDist(v, 8));
        float g = 0.82f + 0.12f * n + 0.05f * fine;
        if (big > 0.485f) g *= 0.5f;
        else if (small > 0.49f) g *= 0.8f;
        float bx = u * 2 - floorf(u * 2), by = v * 2 - floorf(v * 2);
        for (float cx : { 0.06f, 0.94f })
            for (float cy : { 0.06f, 0.94f })
                if ((bx - cx) * (bx - cx) + (by - cy) * (by - cy) < 0.0006f) g *= 0.55f;
        return glm::vec4(g, g, g, 0.45f);
    }
    default: {
        // 상자: 테두리 틀 + 가로 판자
        float edge = std::max(fabsf(u - 0.5f), fabsf(v - 0.5f));
        float plank = gridDist(v, 5);
        float g = 0.8f + 0.15f * tnoise(u * 6.0f, v * 60.0f, 6, 71) + 0.08f * n;
        if (edge > 0.42f) g *= 0.75f;
        else if (plank > 0.48f) g *= 0.6f;
        return glm::vec4(g, g * 0.97f, g * 0.94f, 0.2f);
    }
    }
}

void createTextures() {
    std::vector<uint8_t> px((size_t)TEX_SIZE * TEX_SIZE * 4 * TEX_COUNT);
    // 층마다 스레드 하나씩 (이미지 생성은 CPU 작업이라 나눠서 빠르게)
    std::vector<std::thread> workers;
    for (int layer = 0; layer < TEX_COUNT; ++layer)
        workers.emplace_back([&px, layer]() {
            for (int y = 0; y < TEX_SIZE; ++y)
                for (int x = 0; x < TEX_SIZE; ++x) {
                    glm::vec4 c = glm::clamp(texel(layer, (x + 0.5f) / TEX_SIZE, (y + 0.5f) / TEX_SIZE), 0.0f, 1.0f);
                    uint8_t* p = &px[(((size_t)layer * TEX_SIZE + y) * TEX_SIZE + x) * 4];
                    for (int k = 0; k < 4; ++k) p[k] = (uint8_t)(c[k] * 255.0f + 0.5f);
                }
        });
    for (auto& w : workers) w.join();

    glGenTextures(1, &albedoArray);
    glBindTexture(GL_TEXTURE_2D_ARRAY, albedoArray);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, TEX_SIZE, TEX_SIZE, TEX_COUNT, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);   // 멀리 있는 면의 반짝임(모아레) 방지
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    if (GLEW_EXT_texture_filter_anisotropic) {
        float maxAniso = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
        glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(8.0f, maxAniso));
    }
}
