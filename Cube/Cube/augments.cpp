// 증강: 방을 깰 때마다 3개 중 하나를 골라 이번 판 동안 플레이어를 영구 강화한다 (R 로 처음부터 하면 초기화)
#include "cube.h"

Stats stats;
std::vector<Augment> augments;      // 단계별로 펼친 전체 목록 (같은 계열은 I, II, III ... 순서)
std::vector<int> ownedAugments;     // 고른 순서
bool augPicking = false;
bool augPickupActive = false;       // 방을 깨면 나오는 증강 물체가 지금 방에 있는지
glm::vec3 augPickupPos(0.0f);
static bool augTaken[27];           // 그 방(칸)의 증강을 이미 받았는지. 방을 나갔다 와도 안 받았으면 다시 나온다
int augOffer[3] = { -1, -1, -1 }, augOfferCount = 0;
static std::vector<bool> augOffered;    // 한 번이라도 후보로 나왔는지 (안 나온 것부터 보여준다)

// 계열마다 단계 수와 한 단계의 효과. 단계는 앞 단계를 가져야 후보에 나온다
// 스킬 강화(skill = true) 계열은 아직 없음: 이 표에 skill 계열을 추가하면 같은 방식으로 후보에 섞인다
struct AugLine { const char* name; const char* desc; int tiers; bool skill; void (*apply)(Stats&); };

static const AugLine LINES[] = {
    { "FIRE RATE", "FIRE RATE +10%", 5, false, [](Stats& s) { s.fireInterval *= 0.9f; } },
    { "BULLET SPEED", "BULLET SPEED +15%", 4, false, [](Stats& s) { s.bulletSpeed *= 1.15f; } },
    { "PIERCE", "BULLETS PIERCE +1 ENEMY", 3, false, [](Stats& s) { s.pierce += 1; } },
    { "MULTISHOT", "+1 BULLET PER SHOT", 3, false, [](Stats& s) { s.pellets += 1; } },
    { "MAX HP", "MAX HP +1 (AND HEAL 1)", 5, false, [](Stats& s) { s.maxHp += 1; } },
    { "MOVE SPEED", "MOVE SPEED +8%", 4, false, [](Stats& s) { s.moveMul *= 1.08f; } },
    { "SPRINT", "SPRINT SPEED +10%", 3, false, [](Stats& s) { s.sprintMul *= 1.1f; } },
    { "JUMP", "JUMP POWER +12%", 3, false, [](Stats& s) { s.jump *= 1.12f; } },
    { "FOCUS TIME", "BULLET TIME DRAINS 15% SLOWER", 4, false, [](Stats& s) { s.focusDrain *= 0.85f; } },
    { "FOCUS REGEN", "BULLET TIME REFILLS 20% FASTER", 3, false, [](Stats& s) { s.focusRegen *= 1.2f; } },
    { "WEB RANGE", "WEB RANGE +12%", 4, false, [](Stats& s) { s.grappleRange *= 1.12f; } },
    { "WEB DASH", "SWING DASH SPEED +12%", 4, false, [](Stats& s) { s.dash *= 1.12f; } },
    { "SWING POWER", "SWING BOOST +20%  MAX SPEED +10%", 3, false, [](Stats& s) { s.pump *= 1.2f; s.swingMax *= 1.1f; } },
    { "AIR CONTROL", "SWING STEERING +25%", 3, false, [](Stats& s) { s.steer *= 1.25f; } },
    { "RELEASE POP", "RELEASE LAUNCH +30%", 2, false, [](Stats& s) { s.releasePop *= 1.3f; } },
    { "RECOVERY", "HEAL 1 HP ON ROOM CLEAR", 2, false, [](Stats& s) { s.clearHeal += 1; } },
    { "VAMPIRE", "HEAL 1 HP EVERY N KILLS (12/9/6)", 3, false, [](Stats& s) { s.killsPerHeal = s.killsPerHeal == 0 ? 12 : s.killsPerHeal - 3; } },
};
const int AUG_LINE_COUNT = (int)(sizeof(LINES) / sizeof(LINES[0]));

static const char* roman(int n) {
    static const char* r[] = { "", "I", "II", "III", "IV", "V", "VI" };
    return n >= 0 && n <= 6 ? r[n] : "?";
}

void initAugments() {
    augments.clear();
    for (int l = 0; l < AUG_LINE_COUNT; ++l)
        for (int t = 1; t <= LINES[l].tiers; ++t)
            augments.push_back({ std::string(LINES[l].name) + " " + roman(t), LINES[l].desc, l, t, LINES[l].skill });
    resetAugments();
}

void resetAugments() {
    stats = Stats{};
    ownedAugments.clear();
    augOffered.assign(augments.size(), false);
    augPicking = false;
    augOfferCount = 0;
    augPickupActive = false;
    for (bool& t : augTaken) t = false;
}

// 깬 방에서 아직 증강을 안 받았으면 바닥의 빈 자리에 증강 물체를 놓는다.
// 바닥 가운데는 아래층으로 가는 포털 자리라 반지름 8 부터 바깥으로 찾는다
void placeAugmentPickup() {
    augPickupActive = currentCell != HUB_CELL && roomCleared[currentCell] && !augTaken[currentCell];
    if (!augPickupActive) return;
    augPickupPos = glm::vec3(8.0f, 0.0f, 0.0f);
    for (int ring = 0; ring < 7; ++ring) {
        bool found = false;
        for (int k = 0; k < 8 && !found; ++k) {
            float ang = k * PI * 0.25f + ring * 0.4f, rad = 8.0f + ring * 2.5f;
            glm::vec3 p(cosf(ang) * rad, 0.0f, sinf(ang) * rad);
            if (spotIsFree(p, 1.0f)) { augPickupPos = p; found = true; }
        }
        if (found) break;
    }
    augPickupPos.y = groundHeightAt(augPickupPos, 0.6f);
}

bool nearAugmentPickup() {
    return augPickupActive && !player.dead && glm::length(player.pos + glm::vec3(0, 0.9f, 0) - (augPickupPos + glm::vec3(0, 1.2f, 0))) < 3.2f;
}

// F: 증강 물체 가까이에서 누르면 카드 3장을 연다
void tryInteract() {
    if (!nearAugmentPickup()) return;
    startAugmentPick();
    if (!augPicking) { augTaken[currentCell] = true; augPickupActive = false; }   // 더 고를 게 없음
}

int augLineTier(int line) {
    int t = 0;
    for (int i : ownedAugments) if (augments[i].line == line) t = std::max(t, augments[i].tier);
    return t;
}

const char* augLineName(int line) { return LINES[line].name; }
int augLineTiers(int line) { return LINES[line].tiers; }

// 방 클리어 직후: 가질 수 있는 것(안 가진 것 + 앞 단계 보유) 중 3개. 아직 후보로 안 나온 것을 먼저 고른다
void startAugmentPick() {
    std::vector<int> fresh, seen;
    for (int i = 0; i < (int)augments.size(); ++i) {
        const Augment& a = augments[i];
        if (std::find(ownedAugments.begin(), ownedAugments.end(), i) != ownedAugments.end()) continue;
        if (augLineTier(a.line) != a.tier - 1) continue;
        (augOffered[i] ? seen : fresh).push_back(i);
    }
    std::shuffle(fresh.begin(), fresh.end(), rng);
    std::shuffle(seen.begin(), seen.end(), rng);
    fresh.insert(fresh.end(), seen.begin(), seen.end());
    augOfferCount = std::min(3, (int)fresh.size());
    for (int k = 0; k < augOfferCount; ++k) { augOffer[k] = fresh[k]; augOffered[fresh[k]] = true; }
    if (augOfferCount == 0) return;     // 더 고를 게 없으면 넘어간다
    augPicking = true;
    glfwSetInputMode(gWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

void chooseAugment(int k) {
    if (!augPicking || k < 0 || k >= augOfferCount) return;
    int id = augOffer[k];
    int oldMax = stats.maxHp;
    LINES[augments[id].line].apply(stats);
    player.hp = std::min(stats.maxHp, player.hp + std::max(0, stats.maxHp - oldMax));
    ownedAugments.push_back(id);
    augPicking = false;
    augTaken[currentCell] = true;
    augPickupActive = false;
    bannerText = "AUGMENT - " + augments[id].name;
    bannerT = 2.5f;
    glfwSetInputMode(gWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    firstMouse = true;
}

// 카드 i 의 화면 사각형 (HUD 픽셀 좌표, y 위 방향). 그리기와 클릭 판정이 같이 쓴다
void augmentCardRect(int i, int w, int h, float& x, float& y, float& cw, float& ch) {
    float ui = h / 720.0f;
    cw = 300.0f * ui;
    ch = 190.0f * ui;
    float gap = 30.0f * ui, total = augOfferCount * cw + (augOfferCount - 1) * gap;
    x = w * 0.5f - total * 0.5f + i * (cw + gap);
    y = h * 0.5f - ch * 0.5f - 20.0f * ui;
}

int augmentCardAt(double fbX, double fbY, int w, int h) {
    for (int i = 0; i < augOfferCount; ++i) {
        float x, y, cw, ch;
        augmentCardRect(i, w, h, x, y, cw, ch);
        if (fbX >= x && fbX <= x + cw && fbY >= y && fbY <= y + ch) return i;
    }
    return -1;
}
