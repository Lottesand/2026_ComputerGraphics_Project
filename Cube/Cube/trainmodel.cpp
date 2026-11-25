// 허브 열차 모델(외부 에셋): tools/train/bake_train.py 가 만든 메쉬와 텍스처를 읽어 그린다
// 모델에는 불빛이 없으므로 실내등·전조등·후미등·행선판은 여기서 덧붙인다
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "cube.h"
#undef APIENTRY     // GLEW 가 정의한 것과 겹친다
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

bool useTrainModel = true;              // Shift + 3 으로 예전 상자 열차와 비교
static bool trainReady = false;
static Mesh trainMesh;
static GLuint trainTex[3] = { 0, 0, 0 };    // 색, ORM(G 거칠기, B 금속), 노멀
static const int TRAIN_UNIT = 4;            // 텍스처 유닛 4, 5, 6
static const char* const TRAIN_DIR = "assets/train/";

static std::string findAsset(const std::string& name) {
    for (const char* base : { "", "Cube/" }) {
        std::string p = std::string(base) + TRAIN_DIR + name;
        if (std::ifstream(p, std::ios::binary).is_open()) return p;
    }
    return "";
}

// JPEG/PNG → RGBA8 (Windows 내장 WIC 사용, 첫 줄이 이미지의 맨 위 = glTF UV 기준 그대로)
static bool loadImage(const std::string& path, std::vector<uint8_t>& px, UINT& w, UINT& h) {
    IWICImagingFactory* fac = nullptr;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    std::wstring wp(path.begin(), path.end());      // 경로는 위의 ASCII 상수뿐
    bool ok = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fac))) &&
              SUCCEEDED(fac->CreateDecoderFromFilename(wp.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) &&
              SUCCEEDED(dec->GetFrame(0, &frame)) && SUCCEEDED(fac->CreateFormatConverter(&conv)) &&
              SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)) &&
              SUCCEEDED(conv->GetSize(&w, &h));
    if (ok) {
        px.resize((size_t)w * h * 4);
        ok = SUCCEEDED(conv->CopyPixels(nullptr, w * 4, (UINT)px.size(), px.data()));
    }
    if (conv) conv->Release();
    if (frame) frame->Release();
    if (dec) dec->Release();
    if (fac) fac->Release();
    return ok;
}

static bool loadTexture(const char* name, GLuint& tex) {
    std::vector<uint8_t> px;
    UINT w = 0, h = 0;
    std::string path = findAsset(name);
    if (path.empty() || !loadImage(path, px, w, h)) return false;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (GLEW_EXT_texture_filter_anisotropic) {
        float maxAniso = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(8.0f, maxAniso));
    }
    return true;
}

static bool loadTrainMesh() {
    std::string path = findAsset("train_car.mesh");
    std::ifstream f(path, std::ios::binary);
    char magic[4] = {};
    uint32_t nv = 0, ni = 0;
    if (path.empty() || !f.read(magic, 4) || std::string(magic, 4) != "CUBM") return false;
    f.read((char*)&nv, 4);
    f.read((char*)&ni, 4);
    if (!f || nv == 0 || nv > 1000000 || ni == 0 || ni > 6000000) return false;
    std::vector<float> v((size_t)nv * 8);
    std::vector<uint32_t> idx(ni);
    f.read((char*)v.data(), v.size() * sizeof(float));
    f.read((char*)idx.data(), idx.size() * sizeof(uint32_t));
    if (!f) return false;
    // 다른 메쉬처럼 glDrawArrays 로 그리도록 인덱스를 풀어 담는다
    std::vector<float> d;
    d.reserve((size_t)ni * 8);
    for (uint32_t i : idx) {
        if (i >= nv) return false;
        d.insert(d.end(), v.begin() + (size_t)i * 8, v.begin() + (size_t)i * 8 + 8);
    }
    glGenVertexArrays(1, &trainMesh.vao);
    glGenBuffers(1, &trainMesh.vbo);
    glBindVertexArray(trainMesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, trainMesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, d.size() * sizeof(float), d.data(), GL_STATIC_DRAW);
    const GLsizei stride = 8 * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glVertexAttribPointer(10, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    for (int a : { 0, 1, 10 }) glEnableVertexAttribArray(a);
    glBindVertexArray(0);
    trainMesh.count = (GLsizei)ni;
    return true;
}

void createTrainModel() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);     // 이미 초기화돼 있어도 된다
    trainReady = loadTrainMesh() && loadTexture("train_color.jpg", trainTex[0]) &&
                 loadTexture("train_orm.jpg", trainTex[1]) && loadTexture("train_normal.png", trainTex[2]);
    if (SUCCEEDED(hr)) CoUninitialize();
    if (!trainReady) std::cerr << "열차 모델을 읽지 못해 상자 열차로 그립니다 (assets/train)\n";
    // 샘플러 유닛은 모델이 없어도 정해 둔다 (shadowMap 등 다른 종류의 샘플러와 유닛이 겹치지 않게)
    glUseProgram(sceneProg);
    const char* names[3] = { "modelColor", "modelOrm", "modelNormal" };
    for (int i = 0; i < 3; ++i) glUniform1i(U(sceneProg, names[i]), TRAIN_UNIT + i);
    glUseProgram(0);
}

bool trainModelOn() { return useTrainModel && trainReady; }

void bindTrainTextures() {
    for (int i = 0; i < 3; ++i) {
        glActiveTexture(GL_TEXTURE0 + TRAIN_UNIT + i);
        glBindTexture(GL_TEXTURE_2D, trainTex[i]);
    }
    glActiveTexture(GL_TEXTURE0);
}

// 객차 한 칸. C = carWorld (x = 진행 방향). 모델은 빨간 후미등이 +X 쪽이라 뒤로 돌려 그린다
void drawTrainCar(const glm::mat4& C, bool first, bool last) {
    const int MODEL = 5, CABIN = 6;     // scene_fragment.glsl 의 pattern
    Material skin{ glm::vec3(1.25f), 0.9f, 64.0f, 0.0f, MODEL };                    // color = 어두운 텍스처 밝기 보정
    Material cabin{ glm::vec3(1.0f, 0.8f, 0.52f), 0.0f, 1.0f, 1.8f, CABIN };
    Material head{ glm::vec3(1.0f, 0.95f, 0.82f), 0.0f, 1.0f, 7.0f, 0 };
    Material tail{ glm::vec3(1.0f, 0.1f, 0.06f), 0.0f, 1.0f, 6.0f, 0 };
    Material sign{ glm::vec3(1.0f, 0.6f, 0.15f), 0.0f, 1.0f, 2.6f, 0 };
    drawMesh(trainMesh, C * glm::rotate(glm::mat4(1.0f), PI, glm::vec3(0, 1, 0)), skin);
    if (shadowPass) return;
    // 실내등: 창문이 뚫려 있어 벽 바로 안쪽의 빛나는 상자가 창으로만 보인다
    drawMesh(cubeMesh, C * TRS({ 0, 0.48f, 0 }, { 6.3f, 0.9f, 2.14f }), cabin);
    for (int e = -1; e <= 1; e += 2) {
        if (!(e > 0 ? first : last)) continue;
        drawPart(C * TRS({ e * 3.33f, 1.03f, 0 }, { 0.04f, 0.15f, 0.84f }), sign);
        for (int s = -1; s <= 1; s += 2)
            drawPart(C * TRS({ e * 3.44f, -0.405f, s * 0.79f }, { 0.05f, 0.11f, 0.3f }), e > 0 ? head : tail);
    }
}
