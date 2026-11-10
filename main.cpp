// Orbitto - a cozy adventure starring the Amir 14 Games mascot
// Nintendo Switch homebrew (C++ / SDL2 / libnx). Also builds on desktop for testing.
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <unordered_map>
#include <functional>
#include <sys/stat.h>
#ifdef __SWITCH__
#include <switch.h>
#define ASSET_DIR "romfs:/"
#define SAVE_DIR "sdmc:/switch/Orbitto"
#define SAVE_FILE "sdmc:/switch/Orbitto/save.dat"
#else
#define ASSET_DIR "flat/"
#define SAVE_DIR "."
#define SAVE_FILE "./cc_save.dat"
#endif

// ------------------------------------------------------------------ constants
static const int SW = 1280, SH = 720;
static const int WORLD_W = 2304, WORLD_H = 1536;
static const float PI = 3.14159265f;
#define TITLE_NAME "Orbitto"
#define STUDIO_NAME "Amir 14 Games"

static SDL_Renderer* R = nullptr;
static SDL_Window* WIN = nullptr;
struct Col { Uint8 r, g, b, a; };
static const Col INK = {38, 42, 38, 255};
static const Col MINT = {190, 228, 180, 255};
static const Col SAGE = {110, 140, 105, 255};

static float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float frand() { return (float)rand() / (float)RAND_MAX; }

// ------------------------------------------------------------------ drawing helpers
static void setc(Col c) { SDL_SetRenderDrawColor(R, c.r, c.g, c.b, c.a); }
static void fillRectC(int x, int y, int w, int h, Col c) { setc(c); SDL_Rect r = {x, y, w, h}; SDL_RenderFillRect(R, &r); }
static void fillEllipse(int cx, int cy, int rx, int ry, Col c) {
    setc(c);
    for (int dy = -ry; dy <= ry; dy++) {
        float t = (float)dy / (float)(ry ? ry : 1);
        int w = (int)(rx * std::sqrt(std::max(0.f, 1.f - t * t)));
        SDL_RenderDrawLine(R, cx - w, cy + dy, cx + w, cy + dy);
    }
}
static void fillCircle(int cx, int cy, int r, Col c) { fillEllipse(cx, cy, r, r, c); }
static void blob(int cx, int cy, int rx, int ry, Col fill, int th = 6) { fillEllipse(cx, cy, rx + th, ry + th, INK); fillEllipse(cx, cy, rx, ry, fill); }
static void fillTri(float x1, float y1, float x2, float y2, float x3, float y3, Col c) {
    setc(c);
    int miny = (int)std::min({y1, y2, y3}), maxy = (int)std::max({y1, y2, y3});
    for (int y = miny; y <= maxy; y++) {
        float xs[3]; int n = 0;
        auto edge = [&](float ax, float ay, float bx, float by) {
            if (((ay <= y && by > y) || (by <= y && ay > y)) && n < 3) { float t = (y - ay) / (by - ay); xs[n++] = ax + t * (bx - ax); }
        };
        edge(x1, y1, x2, y2); edge(x2, y2, x3, y3); edge(x3, y3, x1, y1);
        if (n >= 2) { float a = std::min(xs[0], xs[1]), b = std::max(xs[0], xs[1]); SDL_RenderDrawLine(R, (int)a, y, (int)b, y); }
    }
}
static void fillStar(float cx, float cy, float ro, float ri, Col c, float rot = -PI / 2) {
    float px[10], py[10];
    for (int i = 0; i < 10; i++) { float a = rot + i * PI / 5; float r = (i % 2) ? ri : ro; px[i] = cx + std::cos(a) * r; py[i] = cy + std::sin(a) * r; }
    for (int i = 0; i < 10; i++) { int j = (i + 1) % 10; fillTri(cx, cy, px[i], py[i], px[j], py[j], c); }
}
static void fillHeart(float cx, float cy, float s, Col c) {
    fillCircle((int)(cx - s * 0.5f), (int)(cy - s * 0.3f), (int)(s * 0.55f), c);
    fillCircle((int)(cx + s * 0.5f), (int)(cy - s * 0.3f), (int)(s * 0.55f), c);
    fillTri(cx - s * 1.03f, cy - s * 0.1f, cx + s * 1.03f, cy - s * 0.1f, cx, cy + s * 1.05f, c);
}

struct Spr { SDL_Texture* t = nullptr; int w = 0, h = 0; };  // w,h = logical size (texture is 2x)
static SDL_Texture* beginTarget(int w, int h) {
    SDL_Texture* t = SDL_CreateTexture(R, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetRenderTarget(R, t);
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(R, 0, 0, 0, 0);
    SDL_RenderClear(R);
    return t;
}
static void endTarget() { SDL_SetRenderTarget(R, nullptr); }
static Spr mk(int w, int h, const std::function<void()>& f) {
    Spr s; s.w = w; s.h = h; s.t = beginTarget(w * 2, h * 2); f(); endTarget();
    SDL_SetTextureScaleMode(s.t, SDL_ScaleModeLinear);
    return s;
}
static void drawSpr(const Spr& s, float x, float y, float sc = 1.f, bool flip = false, float ax = 0.5f, float ay = 1.f, double rot = 0) {
    SDL_Rect d = {(int)(x - s.w * sc * ax), (int)(y - s.h * sc * ay), (int)(s.w * sc), (int)(s.h * sc)};
    SDL_RenderCopyEx(R, s.t, nullptr, &d, rot, nullptr, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}
static Spr loadImg(const char* name) {
    Spr s; std::string p = std::string(ASSET_DIR) + name;
    SDL_Surface* sf = IMG_Load(p.c_str());
    if (!sf) { fprintf(stderr, "missing %s\n", p.c_str()); s.w = s.h = 8; s.t = beginTarget(8, 8); endTarget(); return s; }
    s.t = SDL_CreateTextureFromSurface(R, sf); s.w = sf->w; s.h = sf->h; SDL_FreeSurface(sf);
    SDL_SetTextureBlendMode(s.t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(s.t, SDL_ScaleModeLinear);
    return s;
}

// ------------------------------------------------------------------ text
static TTF_Font *F18, *F24, *F32, *FB72, *FB30;
struct TKey { TTF_Font* f; Uint32 c; std::string s; bool operator==(const TKey& o) const { return f == o.f && c == o.c && s == o.s; } };
struct TKeyH { size_t operator()(const TKey& k) const { return std::hash<std::string>()(k.s) ^ (size_t)k.f ^ k.c; } };
struct TVal { SDL_Texture* t; int w, h; };
static std::unordered_map<TKey, TVal, TKeyH> tcache;
static TVal getText(TTF_Font* f, const std::string& s, SDL_Color c) {
    TKey k{f, (Uint32)(c.r | c.g << 8 | c.b << 16 | c.a << 24), s};
    auto it = tcache.find(k);
    if (it != tcache.end()) return it->second;
    if (tcache.size() > 500) { for (auto& p : tcache) SDL_DestroyTexture(p.second.t); tcache.clear(); }
    SDL_Surface* sf = TTF_RenderUTF8_Blended(f, s.empty() ? " " : s.c_str(), c);
    TVal v{nullptr, 0, 0};
    if (sf) { v.t = SDL_CreateTextureFromSurface(R, sf); v.w = sf->w; v.h = sf->h; SDL_FreeSurface(sf); }
    tcache[k] = v; return v;
}
static void text(TTF_Font* f, const std::string& s, int x, int y, SDL_Color c = {255, 255, 255, 255}, int align = 0, bool shadow = true) {
    if (s.empty()) return;
    TVal v = getText(f, s, c); if (!v.t) return;
    int dx = align == 1 ? x - v.w / 2 : (align == 2 ? x - v.w : x);
    if (shadow) { TVal sh = getText(f, s, {38, 42, 38, 200}); SDL_Rect d2 = {dx + 2, y + 2, sh.w, sh.h}; SDL_RenderCopy(R, sh.t, nullptr, &d2); }
    SDL_Rect d = {dx, y, v.w, v.h}; SDL_RenderCopy(R, v.t, nullptr, &d);
}
static void textWrapped(TTF_Font* f, const std::string& s, int x, int y, int w, SDL_Color c) {
    if (s.empty()) return;
    SDL_Surface* sf = TTF_RenderUTF8_Blended_Wrapped(f, s.c_str(), c, w); if (!sf) return;
    SDL_Texture* t = SDL_CreateTextureFromSurface(R, sf); SDL_Rect d = {x, y, sf->w, sf->h};
    SDL_RenderCopy(R, t, nullptr, &d); SDL_DestroyTexture(t); SDL_FreeSurface(sf);
}

// ------------------------------------------------------------------ audio (tiny synth)
struct Voice { float f = 0, t = 0, dur = 0, vol = 0, slide = 0; int wave = 0; bool on = false; };
static Voice voices[10];
static SDL_AudioDeviceID audioDev = 0;
static void audioCb(void*, Uint8* stream, int len) {
    Sint16* o = (Sint16*)stream; int n = len / 2;
    for (int i = 0; i < n; i++) {
        float s = 0;
        for (auto& v : voices) {
            if (!v.on) continue;
            float fr = v.f + v.slide * v.t;
            float ph = std::fmod(fr * v.t, 1.f);
            float w = v.wave == 0 ? std::sin(ph * 2 * PI) : (ph < 0.5f ? 1.f : -1.f) * 0.6f;
            float env = std::min(1.f, v.t * 80.f) * std::max(0.f, 1.f - v.t / v.dur);
            s += w * v.vol * env; v.t += 1.f / 22050.f; if (v.t >= v.dur) v.on = false;
        }
        o[i] = (Sint16)(clampf(s, -1.f, 1.f) * 9000);
    }
}
static void blip(float f, float dur, float vol = 0.5f, int wave = 0, float slide = 0) {
    if (!audioDev) return;
    SDL_LockAudioDevice(audioDev);
    for (auto& v : voices) if (!v.on) { v.f = f; v.t = 0.0001f; v.dur = dur; v.vol = vol; v.wave = wave; v.slide = slide; v.on = true; break; }
    SDL_UnlockAudioDevice(audioDev);
}
static void sfxStar() { blip(880, 0.12f, 0.4f); blip(1318, 0.2f, 0.35f, 0, 0); }
static void sfxCrown() { blip(660, 0.15f, 0.4f); blip(880, 0.15f, 0.4f); blip(1320, 0.3f, 0.4f); }
static void sfxTalk() { blip(300 + rand() % 120, 0.04f, 0.25f, 1); }
static void sfxHop() { blip(420, 0.18f, 0.35f, 0, 900); }
static void sfxMeow() { blip(700, 0.25f, 0.3f, 0, -900); }
static void sfxUi() { blip(520, 0.06f, 0.3f, 1); }
static void sfxDone() { blip(523, 0.14f, 0.4f); blip(659, 0.14f, 0.4f); blip(784, 0.14f, 0.4f); blip(1046, 0.4f, 0.4f); }

// ------------------------------------------------------------------ assets
static Spr sHeadWink, sHeadHappy, sHeadAngry, sHeadSleepy, sCrown, sStandBody, sBodyBack;
static Spr sPoseSit, sPoseSleep, sPoseLie, sPoseBack, sPoseWalk, sPoseJump, sTitle, sScarf;
static Spr sTreeA, sTreeB, sBush, sRock, sHouse, sBench, sSign, sStump, sLily, sLogs, sStarItem, sHeartP, sSpark, sGlow, sShadow;
static Spr sBunny, sFrog, sOwl, sFox;
static SDL_Texture* groundTex = nullptr;

static const Col GRASS1 = {152, 196, 138, 255}, GRASS2 = {140, 186, 128, 255};
static const Col PATHC = {222, 205, 160, 255}, PATHE = {196, 176, 128, 255};
static const Col WATER = {128, 190, 200, 255}, SAND = {232, 220, 178, 255};

static void buildSprites() {
    sHeadWink = loadImg("head_wink.png"); sHeadHappy = loadImg("head_happy.png"); sHeadAngry = loadImg("head_angry.png");
    sHeadSleepy = loadImg("head_sleepy.png"); sCrown = loadImg("crown.png"); sStandBody = loadImg("stand_body.png");
    sBodyBack = loadImg("body_back.png"); sPoseSit = loadImg("pose_sit.png"); sPoseSleep = loadImg("pose_sleep.png");
    sPoseLie = loadImg("pose_lie.png"); sPoseBack = loadImg("pose_back.png"); sPoseWalk = loadImg("pose_walk.png");
    sPoseJump = loadImg("pose_jump.png"); sTitle = loadImg("title_mascot.png"); sScarf = loadImg("scarf.png");

    auto tree = [](Col leaf, Col leaf2) {
        return mk(140, 180, [=] {
            fillRectC(2 * 58, 2 * 112, 2 * 24, 2 * 64, INK); fillRectC(2 * 61, 2 * 112, 2 * 18, 2 * 60, {128, 98, 70, 255});
            blob(2 * 70, 2 * 82, 2 * 46, 2 * 40, leaf); blob(2 * 36, 2 * 100, 2 * 30, 2 * 26, leaf); blob(2 * 104, 2 * 100, 2 * 30, 2 * 26, leaf);
            fillEllipse(2 * 70, 2 * 82, 2 * 44, 2 * 38, leaf); fillEllipse(2 * 36, 2 * 100, 2 * 28, 2 * 24, leaf); fillEllipse(2 * 104, 2 * 100, 2 * 28, 2 * 24, leaf);
            fillEllipse(2 * 56, 2 * 66, 2 * 18, 2 * 12, leaf2); fillEllipse(2 * 90, 2 * 92, 2 * 10, 2 * 7, leaf2);
        });
    };
    sTreeA = tree({118, 164, 108, 255}, {150, 190, 130, 255});
    sTreeB = tree({96, 138, 104, 255}, {128, 168, 128, 255});
    sBush = mk(90, 64, [] {
        blob(2 * 28, 2 * 40, 2 * 22, 2 * 18, {112, 160, 104, 255}); blob(2 * 62, 2 * 40, 2 * 22, 2 * 18, {112, 160, 104, 255}); blob(2 * 45, 2 * 32, 2 * 24, 2 * 20, {124, 172, 112, 255});
        fillCircle(2 * 30, 2 * 38, 2 * 3, {230, 140, 150, 255}); fillCircle(2 * 58, 2 * 44, 2 * 3, {230, 140, 150, 255}); fillCircle(2 * 46, 2 * 28, 2 * 3, {230, 140, 150, 255});
    });
    sRock = mk(80, 56, [] { blob(2 * 40, 2 * 34, 2 * 30, 2 * 18, {160, 164, 158, 255}); fillEllipse(2 * 32, 2 * 28, 2 * 12, 2 * 6, {190, 194, 186, 255}); });
    sHouse = mk(280, 250, [] {
        fillRectC(2 * 34 - 6, 2 * 100 - 6, 2 * 212 + 12, 2 * 140 + 6, INK); fillRectC(2 * 34, 2 * 100, 2 * 212, 2 * 140, {238, 228, 204, 255});
        fillRectC(2 * 34, 2 * 200, 2 * 212, 2 * 40, {222, 210, 184, 255});
        fillTri(2 * 8, 2 * 106, 2 * 140, 2 * 16, 2 * 272, 2 * 106, INK);
        fillTri(2 * 22, 2 * 100, 2 * 140, 2 * 28, 2 * 258, 2 * 100, SAGE);
        fillTri(2 * 60, 2 * 96, 2 * 140, 2 * 40, 2 * 220, 2 * 96, {128, 160, 120, 255});
        fillRectC(2 * 200 - 4, 2 * 30 - 4, 2 * 30 + 8, 2 * 46, INK); fillRectC(2 * 200, 2 * 30, 2 * 30, 2 * 46, {170, 150, 130, 255});
        blob(2 * 140, 2 * 190, 2 * 24, 2 * 44, {128, 98, 70, 255}, 6); fillRectC(2 * 116, 2 * 190, 2 * 48, 2 * 50, {128, 98, 70, 255});
        fillCircle(2 * 154, 2 * 200, 2 * 3, {240, 220, 140, 255});
        for (int i = 0; i < 2; i++) { int wx = i ? 2 * 192 : 2 * 56; fillRectC(wx - 6, 2 * 130 - 6, 2 * 40 + 12, 2 * 40 + 12, INK); fillRectC(wx, 2 * 130, 2 * 40, 2 * 40, {255, 232, 168, 255}); fillRectC(wx + 2 * 19, 2 * 130, 4, 2 * 40, INK); fillRectC(wx, 2 * 149, 2 * 40, 4, INK); }
    });
    sBench = mk(130, 70, [] {
        fillRectC(2 * 14, 2 * 38, 2 * 14, 2 * 28, INK); fillRectC(2 * 102, 2 * 38, 2 * 14, 2 * 28, INK);
        fillRectC(2 * 8 - 4, 2 * 24 - 4, 2 * 114 + 8, 2 * 20 + 8, INK); fillRectC(2 * 8, 2 * 24, 2 * 114, 2 * 20, {170, 130, 96, 255});
        fillRectC(2 * 8 - 4, 2 * 6 - 4, 2 * 114 + 8, 2 * 16 + 8, INK); fillRectC(2 * 8, 2 * 6, 2 * 114, 2 * 16, {188, 148, 110, 255});
        fillRectC(2 * 16, 2 * 40, 2 * 10, 2 * 24, {128, 98, 70, 255}); fillRectC(2 * 104, 2 * 40, 2 * 10, 2 * 24, {128, 98, 70, 255});
    });
    sSign = mk(80, 100, [] {
        fillRectC(2 * 34, 2 * 40, 2 * 12, 2 * 58, INK); fillRectC(2 * 37, 2 * 40, 2 * 6, 2 * 56, {128, 98, 70, 255});
        fillRectC(2 * 8 - 4, 2 * 8 - 4, 2 * 64 + 8, 2 * 40 + 8, INK); fillRectC(2 * 8, 2 * 8, 2 * 64, 2 * 40, {206, 176, 138, 255});
        fillStar(2 * 40, 2 * 28, 2 * 12, 2 * 5, {200, 196, 120, 255});
    });
    sStump = mk(80, 56, [] { blob(2 * 40, 2 * 40, 2 * 26, 2 * 10, {150, 112, 82, 255}); fillRectC(2 * 14, 2 * 20, 2 * 52, 2 * 20, {150, 112, 82, 255}); fillRectC(2 * 14 - 6, 2 * 20, 6, 2 * 20, INK); fillRectC(2 * 66, 2 * 20, 6, 2 * 20, INK); blob(2 * 40, 2 * 20, 2 * 26, 2 * 10, {212, 180, 140, 255}); });
    sLily = mk(64, 32, [] { fillEllipse(2 * 32, 2 * 16, 2 * 26, 2 * 11, {96, 150, 100, 255}); fillTri(2 * 32, 2 * 16, 2 * 60, 2 * 6, 2 * 60, 2 * 14, {128, 190, 200, 255}); });
    sLogs = mk(110, 50, [] { blob(2 * 55, 2 * 34, 2 * 44, 2 * 9, {128, 98, 70, 255}, 5); blob(2 * 40, 2 * 26, 2 * 32, 2 * 8, {150, 112, 82, 255}, 5); blob(2 * 70, 2 * 28, 2 * 30, 2 * 8, {140, 106, 78, 255}, 5); });
    sStarItem = mk(56, 56, [] { fillStar(2 * 28, 2 * 30, 2 * 25, 2 * 11, INK); fillStar(2 * 28, 2 * 30, 2 * 20, 2 * 9, {212, 202, 104, 255}); fillStar(2 * 24, 2 * 26, 2 * 8, 2 * 4, {240, 234, 160, 255}); });
    sHeartP = mk(40, 40, [] { fillHeart(2 * 20, 2 * 16, 2 * 11, INK); fillHeart(2 * 20, 2 * 16, 2 * 8, {236, 150, 170, 255}); });
    sSpark = mk(30, 30, [] { fillStar(2 * 15, 2 * 15, 2 * 14, 2 * 4, {255, 250, 200, 255}); });
    sShadow = mk(100, 40, [] { fillEllipse(2 * 50, 2 * 20, 2 * 44, 2 * 14, {20, 30, 20, 70}); });
    sGlow = mk(256, 256, [] {
        for (int r = 126; r > 2; r -= 3) fillCircle(2 * 128, 2 * 128, 2 * r, {255, 190, 110, (Uint8)(5 + (126 - r) * 0.22)});
    });
    SDL_SetTextureBlendMode(sGlow.t, SDL_BLENDMODE_ADD);

    // critters (anchor: bottom centre)
    sBunny = mk(96, 120, [] {
        blob(2 * 30, 2 * 28, 2 * 9, 2 * 26, {244, 238, 228, 255}); blob(2 * 66, 2 * 28, 2 * 9, 2 * 26, {244, 238, 228, 255});
        fillEllipse(2 * 30, 2 * 30, 2 * 4, 2 * 18, {240, 176, 188, 255}); fillEllipse(2 * 66, 2 * 30, 2 * 4, 2 * 18, {240, 176, 188, 255});
        blob(2 * 48, 2 * 96, 2 * 30, 2 * 20, {244, 238, 228, 255}); blob(2 * 48, 2 * 66, 2 * 28, 2 * 24, {250, 244, 236, 255});
        fillCircle(2 * 38, 2 * 64, 2 * 4, INK); fillCircle(2 * 58, 2 * 64, 2 * 4, INK); fillCircle(2 * 37, 2 * 62, 2 * 1, {255, 255, 255, 255}); fillCircle(2 * 57, 2 * 62, 2 * 1, {255, 255, 255, 255});
        fillEllipse(2 * 48, 2 * 72, 2 * 4, 2 * 3, {236, 150, 170, 255}); fillEllipse(2 * 30, 2 * 74, 2 * 5, 2 * 3, {250, 190, 190, 140}); fillEllipse(2 * 66, 2 * 74, 2 * 5, 2 * 3, {250, 190, 190, 140});
    });
    sFrog = mk(100, 90, [] {
        blob(2 * 50, 2 * 62, 2 * 36, 2 * 22, {128, 186, 118, 255}); fillEllipse(2 * 50, 2 * 70, 2 * 26, 2 * 12, {200, 226, 170, 255});
        blob(2 * 32, 2 * 36, 2 * 13, 2 * 13, {128, 186, 118, 255}); blob(2 * 68, 2 * 36, 2 * 13, 2 * 13, {128, 186, 118, 255});
        fillCircle(2 * 32, 2 * 36, 2 * 8, {255, 255, 255, 255}); fillCircle(2 * 68, 2 * 36, 2 * 8, {255, 255, 255, 255});
        fillCircle(2 * 34, 2 * 37, 2 * 4, INK); fillCircle(2 * 66, 2 * 37, 2 * 4, INK);
        setc(INK); for (int i = -1; i <= 1; i++) SDL_RenderDrawLine(R, 2 * 38, 2 * 62 + i, 2 * 62, 2 * 62 + i);
        fillCircle(2 * 24, 2 * 64, 2 * 4, {240, 170, 170, 150}); fillCircle(2 * 76, 2 * 64, 2 * 4, {240, 170, 170, 150});
    });
    sOwl = mk(90, 110, [] {
        fillTri(2 * 20, 2 * 26, 2 * 30, 2 * 6, 2 * 40, 2 * 30, INK); fillTri(2 * 70, 2 * 26, 2 * 60, 2 * 6, 2 * 50, 2 * 30, INK);
        blob(2 * 45, 2 * 66, 2 * 32, 2 * 36, {190, 158, 120, 255}); fillEllipse(2 * 45, 2 * 78, 2 * 20, 2 * 22, {232, 214, 178, 255});
        blob(2 * 31, 2 * 46, 2 * 14, 2 * 14, {248, 240, 224, 255}, 5); blob(2 * 59, 2 * 46, 2 * 14, 2 * 14, {248, 240, 224, 255}, 5);
        fillCircle(2 * 33, 2 * 47, 2 * 6, INK); fillCircle(2 * 57, 2 * 47, 2 * 6, INK); fillCircle(2 * 35, 2 * 45, 2 * 2, {255, 255, 255, 255}); fillCircle(2 * 59, 2 * 45, 2 * 2, {255, 255, 255, 255});
        fillTri(2 * 40, 2 * 54, 2 * 50, 2 * 54, 2 * 45, 2 * 66, INK); fillTri(2 * 42, 2 * 55, 2 * 48, 2 * 55, 2 * 45, 2 * 63, {240, 180, 90, 255});
    });
    sFox = mk(120, 110, [] {
        blob(2 * 92, 2 * 86, 2 * 20, 2 * 12, {236, 152, 84, 255}); fillCircle(2 * 108, 2 * 82, 2 * 8, {252, 246, 236, 255});
        fillTri(2 * 30, 2 * 38, 2 * 38, 2 * 10, 2 * 52, 2 * 36, INK); fillTri(2 * 72, 2 * 38, 2 * 66, 2 * 10, 2 * 52, 2 * 36, INK);
        fillTri(2 * 34, 2 * 36, 2 * 38, 2 * 17, 2 * 48, 2 * 36, {236, 152, 84, 255}); fillTri(2 * 68, 2 * 36, 2 * 66, 2 * 17, 2 * 56, 2 * 36, {236, 152, 84, 255});
        blob(2 * 52, 2 * 84, 2 * 26, 2 * 20, {236, 152, 84, 255}); fillEllipse(2 * 52, 2 * 90, 2 * 14, 2 * 14, {252, 246, 236, 255});
        blob(2 * 52, 2 * 54, 2 * 28, 2 * 22, {240, 160, 92, 255}); fillEllipse(2 * 52, 2 * 64, 2 * 20, 2 * 12, {252, 246, 236, 255});
        fillCircle(2 * 40, 2 * 52, 2 * 4, INK); fillCircle(2 * 64, 2 * 52, 2 * 4, INK); fillCircle(2 * 39, 2 * 50, 2 * 1, {255, 255, 255, 255}); fillCircle(2 * 63, 2 * 50, 2 * 1, {255, 255, 255, 255});
        fillCircle(2 * 52, 2 * 62, 2 * 3, INK);
    });
}

// ------------------------------------------------------------------ world
struct Blk { float x, y, r; };
struct TreeO { float x, y; int v; };
struct Pt { float x, y; };
struct NPC { const char* name; float x, y; int id; Spr* spr; float sc; };
static std::vector<Blk> blocks;
static std::vector<TreeO> trees;
static std::vector<Blk> rocks, bushes;
static std::vector<Pt> pathPts;
static std::vector<int> pathLinks;  // pairs of indices
static const float HOUSE_X = 480, HOUSE_Y = 410;           // base centre of house
static const float POND_X = 1490, POND_Y = 560, POND_RX = 240, POND_RY = 140;
static const float BENCH_X = 860, BENCH_Y = 590;
static const float FIRE_X = 1020, FIRE_Y = 860;
static const float SIGN_X = 650, SIGN_Y = 640;
static std::vector<NPC> npcs;
static const int NSTARS = 40, NCROWNS = 5;
static Pt starPos[NSTARS], crownPos[NCROWNS];

static float segDist(float px, float py, float ax, float ay, float bx, float by) {
    float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
    float t = l2 > 0 ? clampf(((px - ax) * dx + (py - ay) * dy) / l2, 0, 1) : 0;
    float cx = ax + dx * t, cy = ay + dy * t; return std::hypot(px - cx, py - cy);
}
static float distToPath(float x, float y) {
    float d = 1e9f;
    for (size_t i = 0; i + 1 < pathLinks.size(); i += 2) { Pt a = pathPts[pathLinks[i]], b = pathPts[pathLinks[i + 1]]; d = std::min(d, segDist(x, y, a.x, a.y, b.x, b.y)); }
    return d;
}
static bool inPond(float x, float y, float grow) { float dx = (x - POND_X) / (POND_RX + grow), dy = (y - POND_Y) / (POND_RY + grow); return dx * dx + dy * dy < 1; }
static bool blockedAt(float x, float y, float rad) {
    if (x < 110 || y < 120 || x > WORLD_W - 110 || y > WORLD_H - 90) return true;
    if (inPond(x, y, -14 + rad)) return true;
    if (x > HOUSE_X - 112 - rad && x < HOUSE_X + 112 + rad && y > HOUSE_Y - 118 - rad && y < HOUSE_Y - 8 + rad) return true;
    for (auto& b : blocks) if (std::hypot(x - b.x, y - b.y) < b.r + rad) return true;
    return false;
}

static void buildWorld() {
    srand(14);
    pathPts = {{480, 440}, {520, 530}, {640, 590}, {800, 640}, {950, 760}, {1020, 820}, {1100, 610}, {1250, 570}, {420, 770}, {300, 900}};
    pathLinks = {0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 3, 6, 6, 7, 2, 8, 8, 9};
    npcs = {{"Pip", 590, 500, 0, &sBunny, 0.9f}, {"Ribbit", 1250, 535, 1, &sFrog, 0.9f}, {"Hoot", 960, 548, 2, &sOwl, 0.95f}, {"Foxy", 1100, 880, 3, &sFox, 0.95f}};
    blocks.clear(); trees.clear(); rocks.clear(); bushes.clear();
    for (auto& n : npcs) blocks.push_back({n.x, n.y - 6, 24});
    blocks.push_back({FIRE_X, FIRE_Y, 34});
    blocks.push_back({SIGN_X, SIGN_Y, 12});
    blocks.push_back({960, 548, 20});  // owl stump
    auto freeSpot = [&](float x, float y, float rad) {
        if (distToPath(x, y) < 70) return false;
        if (inPond(x, y, 60)) return false;
        if (std::hypot(x - HOUSE_X, y - HOUSE_Y + 60) < 220) return false;
        if (std::hypot(x - 620, y - 560) < 120) return false;
        if (std::hypot(x - BENCH_X, y - BENCH_Y) < 120) return false;
        if (std::hypot(x - FIRE_X, y - FIRE_Y) < 170) return false;
        for (auto& n : npcs) if (std::hypot(x - n.x, y - n.y) < 110) return false;
        for (auto& b : blocks) if (std::hypot(x - b.x, y - b.y) < b.r + rad + 24) return false;
        return true;
    };
    // border forest
    for (float x = 30; x < WORLD_W; x += 66) for (int row = 0; row < 2; row++) {
        TreeO t{x + frand() * 30, 40.f + row * 56 + frand() * 20, rand() % 2}; trees.push_back(t); blocks.push_back({t.x, t.y, 22});
        TreeO u{x + frand() * 30, WORLD_H - 20.f - row * 56 - frand() * 16, rand() % 2}; trees.push_back(u); blocks.push_back({u.x, u.y, 22});
    }
    for (float y = 160; y < WORLD_H - 80; y += 70) for (int col = 0; col < 2; col++) {
        TreeO t{30.f + col * 56 + frand() * 18, y + frand() * 30, rand() % 2}; trees.push_back(t); blocks.push_back({t.x, t.y, 22});
        TreeO u{WORLD_W - 30.f - col * 56 - frand() * 18, y + frand() * 30, rand() % 2}; trees.push_back(u); blocks.push_back({u.x, u.y, 22});
    }
    int tries = 0, placed = 0;
    while (placed < 46 && tries++ < 4000) {
        float x = 170 + frand() * (WORLD_W - 340), y = 190 + frand() * (WORLD_H - 330);
        if (!freeSpot(x, y, 22)) continue;
        trees.push_back({x, y, rand() % 2}); blocks.push_back({x, y, 22}); placed++;
    }
    tries = 0; placed = 0;
    while (placed < 16 && tries++ < 3000) {
        float x = 170 + frand() * (WORLD_W - 340), y = 190 + frand() * (WORLD_H - 330);
        if (!freeSpot(x, y, 24)) continue;
        rocks.push_back({x, y, 22}); blocks.push_back({x, y, 24}); placed++;
    }
    tries = 0; placed = 0;
    while (placed < 14 && tries++ < 3000) {
        float x = 170 + frand() * (WORLD_W - 340), y = 190 + frand() * (WORLD_H - 330);
        if (!freeSpot(x, y, 24)) continue;
        bushes.push_back({x, y, 24}); blocks.push_back({x, y, 22}); placed++;
    }
    // stars
    int n = 0; tries = 0;
    while (n < NSTARS && tries++ < 20000) {
        float x = 160 + frand() * (WORLD_W - 320), y = 170 + frand() * (WORLD_H - 290);
        if (blockedAt(x, y, 30)) continue;
        bool ok = true; for (int i = 0; i < n; i++) if (std::hypot(x - starPos[i].x, y - starPos[i].y) < 110) { ok = false; break; }
        if (ok) starPos[n++] = {x, y};
    }
    // crowns: tucked in quiet corners (pushed to free ground)
    Pt want[NCROWNS] = {{300, 300}, {2040, 260}, {240, 1280}, {2060, 1250}, {1180, 1300}};
    for (int i = 0; i < NCROWNS; i++) {
        Pt p = want[i];
        for (int k = 0; k < 400 && (blockedAt(p.x, p.y, 28)); k++) { p.x += (frand() - 0.5f) * 60; p.y += (frand() - 0.5f) * 60; p.x = clampf(p.x, 160, WORLD_W - 160); p.y = clampf(p.y, 170, WORLD_H - 140); }
        crownPos[i] = p;
    }
}

static void buildGround() {
    groundTex = SDL_CreateTexture(R, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, WORLD_W, WORLD_H);
    SDL_SetRenderTarget(R, groundTex);
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
    srand(77);
    fillRectC(0, 0, WORLD_W, WORLD_H, GRASS1);
    for (int i = 0; i < 260; i++) { int r = 30 + rand() % 80; fillCircle(rand() % WORLD_W, rand() % WORLD_H, r, i % 2 ? Col{GRASS2.r, GRASS2.g, GRASS2.b, 90} : Col{168, 206, 150, 70}); }
    // outer border darker
    for (int i = 0; i < 60; i++) { fillRectC(0, 0, WORLD_W, i, {60, 90, 62, (Uint8)(150 - i * 2)}); fillRectC(0, WORLD_H - i, WORLD_W, i, {60, 90, 62, (Uint8)(150 - i * 2)}); fillRectC(0, 0, i, WORLD_H, {60, 90, 62, (Uint8)(150 - i * 2)}); fillRectC(WORLD_W - i, 0, i, WORLD_H, {60, 90, 62, (Uint8)(150 - i * 2)}); }
    // sand + pond
    fillEllipse((int)POND_X, (int)POND_Y, (int)POND_RX + 46, (int)POND_RY + 40, {206, 192, 150, 255});
    fillEllipse((int)POND_X, (int)POND_Y, (int)POND_RX + 36, (int)POND_RY + 30, SAND);
    fillEllipse((int)POND_X, (int)POND_Y, (int)POND_RX + 6, (int)POND_RY + 6, INK);
    fillEllipse((int)POND_X, (int)POND_Y, (int)POND_RX, (int)POND_RY, WATER);
    fillEllipse((int)POND_X + 10, (int)POND_Y + 14, (int)POND_RX - 50, (int)POND_RY - 36, {112, 176, 190, 255});
    // paths
    for (int pass = 0; pass < 2; pass++)
        for (size_t i = 0; i + 1 < pathLinks.size(); i += 2) {
            Pt a = pathPts[pathLinks[i]], b = pathPts[pathLinks[i + 1]]; float L = std::hypot(b.x - a.x, b.y - a.y);
            for (float t = 0; t <= L; t += 8) { float k = t / L; fillCircle((int)lerpf(a.x, b.x, k), (int)lerpf(a.y, b.y, k), pass ? 32 : 39, pass ? PATHC : PATHE); }
        }
    fillEllipse((int)HOUSE_X, (int)HOUSE_Y + 18, 70, 24, PATHE); fillEllipse((int)HOUSE_X, (int)HOUSE_Y + 18, 64, 20, PATHC);
    fillEllipse((int)FIRE_X, (int)FIRE_Y + 6, 120, 60, PATHE); fillEllipse((int)FIRE_X, (int)FIRE_Y + 6, 112, 54, PATHC);
    for (int i = 0; i < 160; i++) { float x = rand() % WORLD_W, y = rand() % WORLD_H; if (distToPath(x, y) < 22 && !inPond(x, y, 0)) fillEllipse((int)x, (int)y, 3 + rand() % 3, 2 + rand() % 2, {200, 184, 140, 255}); }
    // tufts and flowers
    for (int i = 0; i < 900; i++) {
        float x = rand() % WORLD_W, y = rand() % WORLD_H;
        if (distToPath(x, y) < 44 || inPond(x, y, 50)) continue;
        setc({100, 150, 98, 255});
        SDL_RenderDrawLine(R, (int)x, (int)y, (int)x - 3, (int)y - 8); SDL_RenderDrawLine(R, (int)x, (int)y, (int)x, (int)y - 10); SDL_RenderDrawLine(R, (int)x, (int)y, (int)x + 3, (int)y - 8);
    }
    Col fl[4] = {{255, 250, 240, 255}, {240, 170, 190, 255}, {200, 235, 200, 255}, {245, 222, 130, 255}};
    for (int i = 0; i < 260; i++) {
        float x = rand() % WORLD_W, y = rand() % WORLD_H;
        if (distToPath(x, y) < 50 || inPond(x, y, 60)) continue;
        fillCircle((int)x, (int)y, 7, INK); fillCircle((int)x, (int)y, 5, fl[rand() % 4]); fillCircle((int)x, (int)y, 2, {250, 220, 120, 255});
    }
    SDL_SetRenderTarget(R, nullptr);
}

// ------------------------------------------------------------------ game state
enum Pose { P_IDLE, P_WALK, P_SIT, P_SLEEP, P_HOP };
enum Expr { E_WINK, E_HAPPY, E_ANGRY, E_SLEEPY };
struct Particle { float x, y, vx, vy, life, max; int type; float rot; };
static std::vector<Particle> parts;
struct Firefly { float x, y, a, s; };
static std::vector<Firefly> flies;

struct SaveData { Uint32 magic = 0x43524F57; Uint32 ver = 1; Uint64 starMask = 0; Uint32 crownMask = 0; Sint32 quest[4] = {0, 0, 0, 0}; float hour = 9, px = 640, py = 600, cozy = 0, napT = 0, fireT = 0; Sint32 won = 0; };
static SaveData G;  // live state
static float px = 640, py = 610, pvx = 0, pvy = 0;
static int facing = 0;  // 0 down,1 left,2 right,3 up
static Pose pose = P_IDLE; static float poseT = 0, idleT = 0, walkAnim = 0, hopT = 0;
static Expr emote = E_WINK; static float emoteT = 0; static int emoteCycle = 0;
static float camX = 0, camY = 0, gameTime = 0;
static bool hasTarget = false; static float tgx = 0, tgy = 0;
enum Mode { M_TITLE, M_PLAY, M_MENU };
static Mode mode = M_TITLE; static int menuSel = 0, titleSel = 0;
static bool touchMode = false; static float touchTimer = 0;
static bool quitReq = false;
static float fade = 0; static int fadeDir = 0; static std::function<void()> fadeCb;
static std::string toastS; static float toastT = 0;
static float saveTimer = 0;
// dialog
static std::vector<std::string> dlg; static size_t dlgIdx = 0; static float dlgChars = 0; static std::string dlgName; static std::function<void()> dlgDone; static bool dlgOn = false;

static int starCount() { int n = 0; for (int i = 0; i < NSTARS; i++) if (G.starMask >> i & 1) n++; return n; }
static int crownCount() { int n = 0; for (int i = 0; i < NCROWNS; i++) if (G.crownMask >> i & 1) n++; return n; }
static void toast(const std::string& s, float t = 3.f) { toastS = s; toastT = t; }
static void spawn(float x, float y, float vx, float vy, float life, int type) { parts.push_back({x, y, vx, vy, life, life, type, frand() * 6.28f}); }
static bool saveExists() { FILE* f = fopen(SAVE_FILE, "rb"); if (f) { fclose(f); return true; } return false; }
static void saveGame() {
    mkdir(SAVE_DIR, 0777);
    G.px = px; G.py = py;
    FILE* f = fopen(SAVE_FILE, "wb"); if (!f) return; fwrite(&G, sizeof G, 1, f); fclose(f);
}
static bool loadGame() {
    FILE* f = fopen(SAVE_FILE, "rb"); if (!f) return false;
    SaveData t; size_t n = fread(&t, sizeof t, 1, f); fclose(f);
    if (n != 1 || t.magic != 0x43524F57) return false;
    G = t; px = G.px; py = G.py; return true;
}
static void newGame() { G = SaveData(); px = 640; py = 610; pose = P_IDLE; facing = 0; hasTarget = false; }

static void say(const char* who, std::vector<std::string> lines, std::function<void()> done = nullptr) {
    dlg = lines; dlgIdx = 0; dlgChars = 0; dlgName = who; dlgDone = done; dlgOn = true; sfxUi();
}
static void burst(float x, float y, int n, int type, float spd = 90) { for (int i = 0; i < n; i++) { float a = frand() * 6.28f; spawn(x, y, std::cos(a) * spd * frand(), std::sin(a) * spd * frand() - 40, 0.9f + frand() * 0.5f, type); } }

// quests -------------------------------------------------------------------
struct QDef { const char* who; int type; int goal; std::vector<std::string> intro, prog, done, after; };
static std::vector<QDef> Q;
static void buildQuests() {
    Q = {
        {"Pip", 0, 8, {"Oh! A little crown cat!", "I'm Pip. I adore shiny stars, but they keep slipping out of the sky.", "Could you gather 8 of them? They're sparkling all around the island."}, {"Look for little golden stars in the grass."},
         {"You found them all! They are so sparkly!", "Thank you, friend. Here, have a hug!"}, {"Your stars make the whole meadow feel warmer."}},
        {"Ribbit", 1, 1, {"Ribbit... I lost my tiny crown by the pond.", "It's mint-green and very small. Could you find one for me?", "I think they hide in the quiet corners of the island."}, {"A mint-green crown... somewhere far from here."},
         {"My crown! Oh, it fits perfectly!", "Ribbit! I feel like royalty!"}, {"Ribbit. Life is good by the water."}},
        {"Hoot", 2, 6, {"Hooo... I can never sleep.", "Would you show me how a proper nap is done?", "Sit on the bench over there, close your eyes, and rest a while."}, {"The bench is just by the path. Press Y to sit, and keep resting."},
         {"Hoooo, you looked so peaceful!", "I'm sleepy already. Thank you, little one."}, {"Zzz... hoo... zzz..."}},
        {"Foxy", 3, 10, {"Brrr, it's chilly tonight.", "Would you sit by my campfire with me for a little while?", "Just sit down close to the fire and enjoy the warmth."}, {"Come close to the fire. Press Y to sit."},
         {"Ahh, that was lovely. Warm paws, warm heart.", "Thanks for keeping me company."}, {"The fire crackles happily."}},
    };
}
static void rewardFx() { burst(px, py - 60, 14, 0, 120); burst(px, py - 60, 10, 1, 130); sfxDone(); G.cozy = std::min(100.f, G.cozy + 25); }
static int questsDone() { int n = 0; for (int i = 0; i < 4; i++) if (G.quest[i] == 3) n++; return n; }
static void checkWin() {
    if (questsDone() == 4 && !G.won) {
        G.won = 1;
        say("Orbitto", {"Everyone on the island is smiling.", "The whole place feels cozy now. Thank you for playing!", "A game by " STUDIO_NAME ". Keep wandering as long as you like."}, [] { for (int i = 0; i < 6; i++) burst(px + (frand() - .5f) * 200, py - 80, 10, 4, 160); });
    }
}
static void talkTo(int id) {
    QDef& q = Q[id]; int& st = G.quest[id];
    auto finish = [&q, id, &st] { st = 3; rewardFx(); (void)q; (void)id; checkWin(); };
    if (st == 0) { say(q.who, q.intro, [&st] { st = 1; toast("New quest!"); }); return; }
    if (st == 1) {
        bool met = (q.type == 0 && starCount() >= q.goal) || (q.type == 1 && crownCount() >= q.goal);
        if (met) { say(q.who, q.done, finish); return; }
        std::vector<std::string> l = q.prog;
        if (q.type == 0) l.push_back("You have " + std::to_string(starCount()) + " of " + std::to_string(q.goal) + " stars.");
        say(q.who, l); return;
    }
    if (st == 2) { say(q.who, q.done, finish); return; }
    say(q.who, q.after);
}

// ------------------------------------------------------------------ input
struct In {
    float mx = 0, my = 0; bool run = false;
    bool a = false, b = false, x = false, y = false, plus = false, up = false, down = false, tap = false;
    float tx = 0, ty = 0;
} in;
static std::vector<SDL_Joystick*> joys;
static bool kb[512];
static void openJoys() { for (int i = 0; i < SDL_NumJoysticks(); i++) { SDL_Joystick* j = SDL_JoystickOpen(i); if (j) joys.push_back(j); } }

static void pollInput() {
    in.a = in.b = in.x = in.y = in.plus = in.up = in.down = in.tap = false;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT: quitReq = true; break;
            case SDL_KEYDOWN:
                if (e.key.repeat) break;
                switch (e.key.keysym.sym) {
                    case SDLK_z: case SDLK_RETURN: case SDLK_j: in.a = true; break;
                    case SDLK_x: case SDLK_k: in.b = true; break;
                    case SDLK_c: in.x = true; break;
                    case SDLK_v: in.y = true; break;
                    case SDLK_ESCAPE: case SDLK_p: in.plus = true; break;
                    case SDLK_UP: case SDLK_w: in.up = true; break;
                    case SDLK_DOWN: case SDLK_s: in.down = true; break;
                }
                touchMode = false; break;
            case SDL_JOYBUTTONDOWN:
                touchMode = false;
                switch (e.jbutton.button) {
                    case 0: in.a = true; break; case 1: in.b = true; break; case 2: in.x = true; break; case 3: in.y = true; break;
                    case 10: in.plus = true; break; case 13: in.up = true; break; case 15: in.down = true; break;
                }
                break;
            case SDL_FINGERDOWN: in.tap = true; in.tx = e.tfinger.x * SW; in.ty = e.tfinger.y * SH; touchMode = true; touchTimer = 12; break;
            case SDL_MOUSEBUTTONDOWN: if (e.button.which != SDL_TOUCH_MOUSEID) { in.tap = true; in.tx = (float)e.button.x; in.ty = (float)e.button.y; } break;
            case SDL_JOYDEVICEADDED: { SDL_Joystick* j = SDL_JoystickOpen(e.jdevice.which); if (j) joys.push_back(j); } break;
            default: break;
        }
    }
    const Uint8* ks = SDL_GetKeyboardState(nullptr);
    float mx = 0, my = 0;
    if (ks[SDL_SCANCODE_LEFT] || ks[SDL_SCANCODE_A]) mx -= 1; if (ks[SDL_SCANCODE_RIGHT] || ks[SDL_SCANCODE_D]) mx += 1;
    if (ks[SDL_SCANCODE_UP] || ks[SDL_SCANCODE_W]) my -= 1; if (ks[SDL_SCANCODE_DOWN] || ks[SDL_SCANCODE_S]) my += 1;
    bool run = ks[SDL_SCANCODE_LSHIFT];
    for (auto j : joys) {
        float ax = SDL_JoystickGetAxis(j, 0) / 32767.f, ay = SDL_JoystickGetAxis(j, 1) / 32767.f;
        if (std::fabs(ax) > 0.25f) mx += ax; if (std::fabs(ay) > 0.25f) my += ay;
        if (SDL_JoystickGetButton(j, 12)) mx -= 1; if (SDL_JoystickGetButton(j, 14)) mx += 1;
        if (SDL_JoystickGetButton(j, 13)) my -= 1; if (SDL_JoystickGetButton(j, 15)) my += 1;
        if (SDL_JoystickGetButton(j, 9) || SDL_JoystickGetButton(j, 7)) run = true;
        if (std::fabs(mx) + std::fabs(my) > 0.2f) touchMode = false;
    }
    float l = std::hypot(mx, my); if (l > 1) { mx /= l; my /= l; }
    in.mx = mx; in.my = my; in.run = run;
}

// ------------------------------------------------------------------ gameplay
static float hourNow() { return G.hour; }
static void tint(float h, Uint8& r, Uint8& g, Uint8& b, Uint8& a) {
    struct K { float h; Uint8 r, g, b, a; };
    static const K ks[] = {{0, 20, 25, 70, 150}, {5, 30, 40, 90, 130}, {6.5f, 255, 170, 130, 50}, {8, 255, 230, 200, 0}, {16.5f, 255, 230, 200, 0}, {18, 255, 150, 100, 55}, {19.5f, 60, 50, 110, 110}, {21, 20, 25, 70, 150}, {24, 20, 25, 70, 150}};
    for (int i = 0; i < 8; i++) if (h >= ks[i].h && h <= ks[i + 1].h) {
        float t = (h - ks[i].h) / (ks[i + 1].h - ks[i].h);
        r = (Uint8)lerpf(ks[i].r, ks[i + 1].r, t); g = (Uint8)lerpf(ks[i].g, ks[i + 1].g, t); b = (Uint8)lerpf(ks[i].b, ks[i + 1].b, t); a = (Uint8)lerpf(ks[i].a, ks[i + 1].a, t); return;
    }
    r = g = b = a = 0;
}
static bool isNight() { return G.hour >= 19.5f || G.hour < 5.5f; }

static void setPose(Pose p) { if (pose != p) { pose = p; poseT = 0; } }
static Pt benchSeat() { return {BENCH_X, BENCH_Y + 22}; }
static float nearestDist(float x, float y) { return std::hypot(px - x, py - y); }

static void doEmote() {
    emoteCycle = (emoteCycle + 1) % 3;
    emoteT = 1.6f; sfxMeow();
    if (emoteCycle == 0) { emote = E_HAPPY; for (int i = 0; i < 4; i++) spawn(px + (frand() - .5f) * 50, py - 90, (frand() - .5f) * 40, -50 - frand() * 30, 1.2f, 0); toast("Mrrow!", 1.2f); }
    else if (emoteCycle == 1) { emote = E_WINK; burst(px, py - 80, 6, 1, 90); toast("*wink*", 1.2f); }
    else { emote = E_ANGRY; burst(px, py - 70, 6, 3, 70); toast("Hmph!", 1.2f); }
}
static void interact() {
    // house door
    if (nearestDist(HOUSE_X, HOUSE_Y + 14) < 70) {
        if (isNight()) { say("Home", {"A cozy bed, a warm blanket... Sleep until morning?"}, [] { fadeDir = 1; fadeCb = [] { G.hour = 7.0f; saveGame(); toast("Good morning!"); G.cozy = std::min(100.f, G.cozy + 6); }; }); }
        else say("Home", {"It's too bright to sleep. Come back when the stars are out."});
        return;
    }
    for (auto& n : npcs) if (nearestDist(n.x, n.y) < 95) { facing = (n.x < px) ? 1 : 2; talkTo(n.id); return; }
    if (nearestDist(SIGN_X, SIGN_Y) < 90) { say("Sign", {"Welcome to Orbitto's island!", "Collect stars, find hidden crowns, help your neighbours, and rest when you're sleepy."}); return; }
    if (nearestDist(BENCH_X, BENCH_Y) < 100) { Pt s = benchSeat(); px = s.x; py = s.y; facing = 0; setPose(P_SIT); hasTarget = false; return; }
    if (nearestDist(FIRE_X, FIRE_Y) < 120) { setPose(P_SIT); hasTarget = false; toast("Warm and toasty."); G.cozy = std::min(100.f, G.cozy + 1); return; }
    if (inPond(px, py, 70)) { burst(px, py - 20, 10, 2, 80); blip(500, 0.1f, 0.3f); toast("Splash!"); return; }
    doEmote();
}

static void updatePlay(float dt) {
    gameTime += dt;
    G.hour += dt / 20.f; if (G.hour >= 24) G.hour -= 24;       // 1 game hour = 20 s
    if (toastT > 0) toastT -= dt;
    if (touchTimer > 0) touchTimer -= dt;
    if (emoteT > 0) emoteT -= dt;
    saveTimer += dt; if (saveTimer > 40) { saveTimer = 0; saveGame(); }

    // dialog
    if (dlgOn) {
        const std::string& cur = dlg[dlgIdx];
        float before = dlgChars; dlgChars += dt * 45.f;
        if ((int)dlgChars != (int)before && (int)dlgChars % 3 == 0) sfxTalk();
        if (in.a || in.tap) {
            if (dlgChars < cur.size()) dlgChars = (float)cur.size();
            else { dlgIdx++; dlgChars = 0; if (dlgIdx >= dlg.size()) { dlgOn = false; auto cb = dlgDone; dlgDone = nullptr; if (cb) cb(); } else sfxUi(); }
        }
        return;
    }
    if (fadeDir) return;

    // actions
    bool tapHandled = false;
    if (in.tap && touchMode) {
        struct B { float x, y, r; int id; } bts[4] = {{1180, 620, 46, 0}, {1085, 665, 36, 1}, {1190, 520, 36, 2}, {1085, 565, 36, 3}};
        for (auto& b : bts) if (std::hypot(in.tx - b.x, in.ty - b.y) < b.r + 8) {
            tapHandled = true;
            if (b.id == 0) in.a = true; else if (b.id == 1) in.b = true; else if (b.id == 2) in.x = true; else in.y = true;
        }
        if (std::hypot(in.tx - 1240, in.ty - 40) < 40) { tapHandled = true; in.plus = true; }
    }
    if (in.plus) { mode = M_MENU; menuSel = 0; sfxUi(); return; }
    if (in.a) interact();
    if (in.b) doEmote();
    if (in.x && pose != P_HOP) { setPose(P_HOP); hopT = 0; sfxHop(); hasTarget = false; }
    if (in.y) { if (pose == P_SIT || pose == P_SLEEP) setPose(P_IDLE); else if (pose != P_HOP) { setPose(P_SIT); hasTarget = false; } }
    if (in.tap && !tapHandled) {
        float wx = in.tx + camX, wy = in.ty + camY;
        bool onNpc = false;
        for (auto& n : npcs) if (std::fabs(wx - n.x) < 50 && wy > n.y - 110 && wy < n.y + 10 && nearestDist(n.x, n.y) < 120) { talkTo(n.id); onNpc = true; }
        if (!onNpc) { hasTarget = true; tgx = wx; tgy = wy; if (pose == P_SIT || pose == P_SLEEP) setPose(P_IDLE); }
    }

    // movement
    float mx = in.mx, my = in.my;
    bool moved = std::hypot(mx, my) > 0.1f;
    if (moved) hasTarget = false;
    if (!moved && hasTarget) {
        float dx = tgx - px, dy = tgy - py, d = std::hypot(dx, dy);
        if (d < 8) hasTarget = false; else { mx = dx / d; my = dy / d; moved = true; }
    }
    if (pose == P_HOP) { hopT += dt; if (hopT > 0.55f) { setPose(P_IDLE); G.cozy = std::min(100.f, G.cozy + 0.5f); } }
    if ((pose == P_SIT || pose == P_SLEEP) && moved) { setPose(P_IDLE); }
    if (moved && (pose == P_IDLE || pose == P_WALK || pose == P_HOP)) {
        float spd = (in.run ? 270.f : 185.f) * (pose == P_HOP ? 0.7f : 1.f);
        float nx = px + mx * spd * dt, ny = py + my * spd * dt;
        bool bx = blockedAt(nx, py, 13), by = blockedAt(px, ny, 13);
        if (!bx) px = nx; if (!by) py = ny;
        if (bx && by) hasTarget = false;
        if (std::fabs(mx) > std::fabs(my)) facing = mx < 0 ? 1 : 2; else facing = my < 0 ? 3 : 0;
        if (pose != P_HOP) setPose(P_WALK);
        walkAnim += dt * (in.run ? 14.f : 10.f); idleT = 0;
        if ((int)(walkAnim * 0.5f) != (int)((walkAnim - dt * 10.f) * 0.5f) && frand() < 0.5f) spawn(px, py - 4, (frand() - .5f) * 20, -10, 0.5f, 5);
    } else if (pose == P_WALK) setPose(P_IDLE);
    poseT += dt;
    if (pose == P_IDLE) { idleT += dt; if (idleT > 25) { setPose(P_SIT); idleT = 0; } }
    else if (pose == P_SIT) { if (poseT > 7.f) setPose(P_SLEEP); }
    if (pose == P_SIT || pose == P_SLEEP) {
        G.cozy = std::min(100.f, G.cozy + dt * 0.6f);
        if (pose == P_SLEEP && frand() < dt * 0.9f) spawn(px + 24, py - 70, 14, -26, 1.8f, 6);
    }
    // quest events
    if (G.quest[2] == 1 && pose == P_SLEEP && nearestDist(BENCH_X, BENCH_Y) < 110) { G.napT += dt; if (G.napT >= 6) { G.quest[2] = 2; toast("Hoot is waiting for you!"); sfxDone(); } }
    if (G.quest[3] == 1 && (pose == P_SIT || pose == P_SLEEP) && nearestDist(FIRE_X, FIRE_Y) < 150) { G.fireT += dt; if (G.fireT >= 10) { G.quest[3] = 2; toast("Foxy is waiting for you!"); sfxDone(); } }
    // pickups
    for (int i = 0; i < NSTARS; i++) if (!(G.starMask >> i & 1) && std::hypot(px - starPos[i].x, py - 22 - starPos[i].y) < 44) {
        G.starMask |= (Uint64)1 << i; sfxStar(); burst(starPos[i].x, starPos[i].y, 8, 1, 110); G.cozy = std::min(100.f, G.cozy + 1.2f);
        emote = E_HAPPY; emoteT = 0.8f;
        if (starCount() == NSTARS) toast("All stars collected!", 4); else toast("Star " + std::to_string(starCount()) + "/" + std::to_string(NSTARS), 1.4f);
    }
    for (int i = 0; i < NCROWNS; i++) if (!(G.crownMask >> i & 1) && std::hypot(px - crownPos[i].x, py - 22 - crownPos[i].y) < 50) {
        G.crownMask |= 1u << i; sfxCrown(); burst(crownPos[i].x, crownPos[i].y, 14, 0, 130); G.cozy = std::min(100.f, G.cozy + 4);
        toast("Mint crown found! (" + std::to_string(crownCount()) + "/" + std::to_string(NCROWNS) + ")", 3);
    }
    // camera
    float tx = clampf(px - SW / 2, 0, WORLD_W - SW), ty = clampf(py - 40 - SH / 2, 0, WORLD_H - SH);
    camX = lerpf(camX, tx, clampf(dt * 6.f, 0, 1)); camY = lerpf(camY, ty, clampf(dt * 6.f, 0, 1));
}

static void updateFx(float dt) {
    for (auto& p : parts) { p.life -= dt; p.x += p.vx * dt; p.y += p.vy * dt; if (p.type == 4) p.vy += 220 * dt; else if (p.type == 0 || p.type == 1) p.vy += 30 * dt; }
    parts.erase(std::remove_if(parts.begin(), parts.end(), [](const Particle& p) { return p.life <= 0; }), parts.end());
    if (fadeDir == 1) { fade += dt * 1.6f; if (fade >= 1) { fade = 1; fadeDir = -1; if (fadeCb) { auto cb = fadeCb; fadeCb = nullptr; cb(); } } }
    else if (fadeDir == -1) { fade -= dt * 1.2f; if (fade <= 0) { fade = 0; fadeDir = 0; } }
    for (auto& f : flies) { f.a += (frand() - .5f) * 4 * dt; f.x += std::cos(f.a) * f.s * dt; f.y += std::sin(f.a) * f.s * dt; if (f.x < 120 || f.x > WORLD_W - 120) f.a += PI; if (f.y < 140 || f.y > WORLD_H - 120) f.a += PI; }
}

// ------------------------------------------------------------------ rendering
static void drawMascot(float x, float y, float t) {
    // shadow
    drawSpr(sShadow, x, y + 6, 1.1f);
    float S = 0.62f, PS = S * 1.45f, bob = 0, hz = 0;
    bool flip = (facing == 1);
    Expr ex = E_WINK;
    if (emoteT > 0) ex = emote;
    else if (pose == P_SIT || pose == P_SLEEP) ex = E_SLEEPY;
    else if (isNight() && idleT > 3) ex = E_SLEEPY;
    else if (std::fmod(t, 4.2f) < 0.12f) ex = E_SLEEPY;  // blink
    if (pose == P_HOP) { hz = std::sin(clampf(hopT / 0.55f, 0, 1) * PI) * 46; }
    if (pose == P_HOP) { drawSpr(sPoseJump, x, y - hz, PS, flip); return; }
    if (pose == P_SLEEP) { drawSpr(sPoseSleep, x, y + 2 + std::sin(t * 1.6f) * 1.5f, PS, flip); return; }
    if (pose == P_SIT) { drawSpr(sPoseSit, x, y + 2 + std::sin(t * 2.f) * 1.2f, PS, flip); return; }
    if (pose == P_WALK) {
        bob = std::fabs(std::sin(walkAnim)) * 7;
        if (facing == 3) { drawSpr(sPoseBack, x, y - bob, PS, std::sin(walkAnim) > 0); return; }
        if (std::sin(walkAnim * 1.0f) > 0.3f) { drawSpr(sPoseWalk, x, y - bob, PS, flip); return; }
    } else {
        if (facing == 3) { drawSpr(sPoseBack, x, y + std::sin(t * 2.f) * 1.2f, PS, false); return; }
        bob = std::sin(t * 2.2f) * 1.6f;
    }
    // front standing: body + swappable head
    const float AX = 72;  // foot anchor inside stand_body
    float bw = (float)sStandBody.w, bh = (float)sStandBody.h;
    float ox = flip ? (bw - AX) : AX;
    float bx = x - ox * S, by = y - bh * S - bob;
    SDL_Rect d = {(int)bx, (int)by, (int)(bw * S), (int)(bh * S)};
    SDL_RenderCopyEx(R, sStandBody.t, nullptr, &d, 0, nullptr, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
    Spr* hd = ex == E_HAPPY ? &sHeadHappy : ex == E_ANGRY ? &sHeadAngry : ex == E_SLEEPY ? &sHeadSleepy : &sHeadWink;
    float hs = 1.1f * S, hw = hd->w * hs, hh = hd->h * hs;
    float hdx = 0 * S, hdy = 3 * S + (pose == P_WALK ? std::sin(walkAnim * 2) * 1.5f : std::sin(t * 2.2f + 1) * 1.f);
    SDL_Rect hd2 = {(int)(flip ? bx + bw * S - hdx - hw : bx + hdx), (int)(by + hdy), (int)hw, (int)hh};
    SDL_RenderCopyEx(R, hd->t, nullptr, &hd2, 0, nullptr, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}

struct DObj { float y; std::function<void()> fn; };

static void drawWorld() {
    float t = gameTime;
    SDL_Rect src = {(int)camX, (int)camY, SW, SH}, dst = {0, 0, SW, SH};
    SDL_RenderCopy(R, groundTex, &src, &dst);
    // animated water sparkles
    for (int i = 0; i < 26; i++) {
        float a = i * 2.399f, rr = std::sqrt((i + 1) / 27.f) * 0.86f;
        float wx = POND_X + std::cos(a) * POND_RX * rr, wy = POND_Y + std::sin(a) * POND_RY * rr;
        float ph = std::sin(t * 1.4f + i); float len = 10 + 8 * ph;
        setc({255, 255, 255, (Uint8)(70 + 60 * ph)});
        SDL_RenderDrawLine(R, (int)(wx - camX - len), (int)(wy - camY), (int)(wx - camX + len), (int)(wy - camY));
    }
    std::vector<DObj> objs;
    auto vis = [&](float x, float y, float m = 160) { return x > camX - m && x < camX + SW + m && y > camY - m && y < camY + SH + m + 100; };
    for (auto& tr : trees) if (vis(tr.x, tr.y)) objs.push_back({tr.y, [&tr] { drawSpr(tr.v ? sTreeB : sTreeA, tr.x - camX, tr.y - camY + 8, 1.0f, false, 0.5f, 0.97f); }});
    for (auto& r : rocks) if (vis(r.x, r.y)) objs.push_back({r.y, [&r] { drawSpr(sRock, r.x - camX, r.y - camY + 14); }});
    for (auto& b : bushes) if (vis(b.x, b.y)) objs.push_back({b.y, [&b] { drawSpr(sBush, b.x - camX, b.y - camY + 14); }});
    objs.push_back({HOUSE_Y, [] { drawSpr(sHouse, HOUSE_X - camX, HOUSE_Y - camY + 14, 1.0f); }});
    objs.push_back({BENCH_Y, [] { drawSpr(sBench, BENCH_X - camX, BENCH_Y - camY + 28, 1.0f); }});
    objs.push_back({SIGN_Y, [] { drawSpr(sSign, SIGN_X - camX, SIGN_Y - camY + 8, 1.0f); }});
    objs.push_back({548.f - 1, [] { drawSpr(sStump, 960 - camX, 548 - camY + 10, 1.0f); }});
    objs.push_back({FIRE_Y - 20, [t] {
        drawSpr(sLogs, FIRE_X - camX, FIRE_Y - camY + 24);
        for (int i = 0; i < 3; i++) {
            float f = std::sin(t * (9 + i * 2.3f) + i) * 0.5f + 0.5f; float h = 34 + f * 14 - i * 8, w = 17 - i * 3;
            Col c = i == 0 ? Col{240, 150, 80, 255} : i == 1 ? Col{250, 200, 100, 255} : Col{255, 240, 170, 255};
            fillTri(FIRE_X - camX - w, FIRE_Y - camY + 6, FIRE_X - camX + w, FIRE_Y - camY + 6, FIRE_X - camX + std::sin(t * 7 + i) * 4, FIRE_Y - camY + 6 - h, c);
            fillCircle((int)(FIRE_X - camX), (int)(FIRE_Y - camY + 4), (int)w, c);
        }
    }});
    for (int i = 0; i < 6; i++) { float lx = POND_X + std::cos(i * 1.4f) * 130, ly = POND_Y + std::sin(i * 1.4f) * 60 + 20; float bobl = std::sin(t * 1.2f + i) * 2; objs.push_back({ly - 30, [lx, ly, bobl] { drawSpr(sLily, lx - camX, ly - camY + bobl, 1.0f, false, 0.5f, 0.5f); }}); }
    for (auto& n : npcs) objs.push_back({n.y, [&n, t] {
        float b = std::sin(t * 2.f + n.id) * 1.5f;
        drawSpr(sShadow, n.x - camX, n.y - camY + 4, 0.9f);
        drawSpr(*n.spr, n.x - camX, n.y - camY + b, n.sc);
        if (nearestDist(n.x, n.y) < 95 && !dlgOn) {
            int qs = G.quest[n.id]; const char* hint = qs == 2 ? "!" : (qs == 0 ? "?" : (qs == 1 ? "..." : ""));
            if (*hint) text(F32, hint, (int)(n.x - camX), (int)(n.y - camY - 130 - std::fabs(std::sin(t * 4)) * 6), {255, 244, 190, 255}, 1);
        }
    }});
    for (int i = 0; i < NSTARS; i++) if (!(G.starMask >> i & 1) && vis(starPos[i].x, starPos[i].y)) {
        float bob = std::sin(t * 3 + i) * 4;
        objs.push_back({starPos[i].y, [i, bob, t] { drawSpr(sShadow, starPos[i].x - camX, starPos[i].y - camY + 16, 0.4f); drawSpr(sStarItem, starPos[i].x - camX, starPos[i].y - camY + bob, 0.7f, false, 0.5f, 0.5f, std::sin(t * 2 + i) * 12); }});
    }
    for (int i = 0; i < NCROWNS; i++) if (!(G.crownMask >> i & 1) && vis(crownPos[i].x, crownPos[i].y)) {
        float bob = std::sin(t * 2.5f + i) * 5;
        objs.push_back({crownPos[i].y, [i, bob] { drawSpr(sGlow, crownPos[i].x - camX, crownPos[i].y - camY, 0.5f, false, 0.5f, 0.5f); drawSpr(sCrown, crownPos[i].x - camX, crownPos[i].y - camY + bob, 0.55f, false, 0.5f, 0.5f); }});
    }
    objs.push_back({py, [] { drawMascot(px - camX, py - camY, gameTime); }});
    std::sort(objs.begin(), objs.end(), [](const DObj& a, const DObj& b) { return a.y < b.y; });
    for (auto& o : objs) o.fn();

    // particles
    for (auto& p : parts) {
        float k = p.life / p.max; float sx = p.x - camX, sy = p.y - camY;
        SDL_SetTextureAlphaMod(sHeartP.t, (Uint8)(255 * clampf(k * 2, 0, 1))); SDL_SetTextureAlphaMod(sSpark.t, (Uint8)(255 * clampf(k * 2, 0, 1)));
        if (p.type == 0) drawSpr(sHeartP, sx, sy, 0.5f + 0.3f * (1 - k), false, 0.5f, 0.5f);
        else if (p.type == 1) drawSpr(sSpark, sx, sy, 0.5f + 0.5f * k, false, 0.5f, 0.5f, p.rot * 40 + p.life * 200);
        else if (p.type == 2) { fillCircle((int)sx, (int)sy, 4, {255, 255, 255, (Uint8)(200 * k)}); }
        else if (p.type == 3) text(F24, "#", (int)sx, (int)sy, {220, 90, 90, (Uint8)(255 * clampf(k * 2, 0, 1))}, 1, false);
        else if (p.type == 4) { Col cs[4] = {{240, 170, 190, 255}, {190, 228, 180, 255}, {245, 222, 130, 255}, {160, 210, 230, 255}}; fillRectC((int)sx, (int)sy, 8, 5, cs[(int)(p.rot * 10) % 4]); }
        else if (p.type == 5) fillCircle((int)sx, (int)sy, 3, {232, 220, 178, (Uint8)(150 * k)});
        else if (p.type == 6) text(F24, "z", (int)sx, (int)sy, {220, 240, 255, (Uint8)(255 * clampf(k * 2, 0, 1))}, 1);
    }
    SDL_SetTextureAlphaMod(sHeartP.t, 255); SDL_SetTextureAlphaMod(sSpark.t, 255);

    // day/night tint + lights
    Uint8 r, g, b, a; tint(G.hour, r, g, b, a);
    if (a) fillRectC(0, 0, SW, SH, {r, g, b, a});
    float nf = a / 150.f;
    float fl = 0.85f + std::sin(t * 11) * 0.08f;
    SDL_SetTextureAlphaMod(sGlow.t, (Uint8)(clampf(60 + 195 * nf, 0, 255)));
    drawSpr(sGlow, FIRE_X - camX, FIRE_Y - camY - 20, 2.2f * fl, false, 0.5f, 0.5f);
    if (nf > 0.25f) {
        SDL_SetTextureAlphaMod(sGlow.t, (Uint8)(200 * nf));
        drawSpr(sGlow, HOUSE_X - 80 - camX, HOUSE_Y - 90 - camY, 0.6f, false, 0.5f, 0.5f); drawSpr(sGlow, HOUSE_X + 72 - camX, HOUSE_Y - 90 - camY, 0.6f, false, 0.5f, 0.5f);
        SDL_SetTextureAlphaMod(sGlow.t, (Uint8)(120 * nf)); drawSpr(sGlow, px - camX, py - 50 - camY, 1.2f, false, 0.5f, 0.5f);
        for (auto& f : flies) { float s = (std::sin(t * 3 + f.a * 5) * 0.5f + 0.5f); SDL_SetTextureAlphaMod(sGlow.t, (Uint8)(210 * nf * s)); drawSpr(sGlow, f.x - camX, f.y - camY, 0.12f, false, 0.5f, 0.5f); }
    }
    SDL_SetTextureAlphaMod(sGlow.t, 255);
}

static void drawBar(int x, int y, int w, int h, float k, Col fill) {
    fillRectC(x - 3, y - 3, w + 6, h + 6, INK); fillRectC(x, y, w, h, {70, 84, 70, 255}); fillRectC(x, y, (int)(w * clampf(k, 0, 1)), h, fill);
}
static void drawHud() {
    // counters
    fillRectC(14, 14, 250, 92, {38, 42, 38, 150});
    drawSpr(sStarItem, 40, 44, 0.6f, false, 0.5f, 0.5f); text(F24, std::to_string(starCount()) + " / " + std::to_string(NSTARS), 66, 28, {255, 255, 255, 255});
    drawSpr(sCrown, 40, 82, 0.34f, false, 0.5f, 0.5f); text(F24, std::to_string(crownCount()) + " / " + std::to_string(NCROWNS), 66, 66);
    // cozy bar
    text(F18, "Cozy", 290, 14, {255, 255, 255, 255}); drawBar(290, 40, 170, 14, G.cozy / 100.f, {236, 160, 176, 255});
    // clock
    int hh = (int)G.hour, mm = (int)((G.hour - hh) * 60) / 10 * 10; char buf[16]; snprintf(buf, sizeof buf, "%02d:%02d", hh, mm);
    text(F24, buf, SW - 60, 16, {255, 255, 255, 255}, 2);
    text(F18, isNight() ? "night" : (G.hour < 12 ? "morning" : (G.hour < 17 ? "afternoon" : "evening")), SW - 60, 46, {220, 235, 220, 255}, 2);
    // quest tracker
    int qy = SH - 28;
    for (int i = 3; i >= 0; i--) {
        if (G.quest[i] == 0 || G.quest[i] == 3) continue;
        std::string what;
        if (G.quest[i] == 2) what = "go talk to them!";
        else if (i == 0) what = "collect stars " + std::to_string(std::min(starCount(), 8)) + "/8";
        else if (i == 1) what = "find a mint crown";
        else if (i == 2) what = "nap on the bench (Y)";
        else what = "sit by the campfire (Y)";
        text(F18, std::string(Q[i].who) + ": " + what, 20, qy);
        qy -= 24;
    }
    if (toastT > 0) {
        TVal v = getText(F24, toastS, {255, 255, 255, 255}); int w = v.w + 40;
        fillRectC(SW / 2 - w / 2 - 3, 117, w + 6, 44, INK); fillRectC(SW / 2 - w / 2, 120, w, 38, {96, 128, 98, 255});
        text(F24, toastS, SW / 2, 124, {255, 255, 255, 255}, 1);
    }
    // hints / touch buttons
    if (touchMode || touchTimer > 0) {
        struct B { float x, y, r; const char* l; } bts[4] = {{1180, 620, 46, "Talk"}, {1085, 665, 36, "Meow"}, {1190, 520, 36, "Hop"}, {1085, 565, 36, "Rest"}};
        for (auto& b : bts) { fillCircle((int)b.x, (int)b.y, (int)b.r + 3, {38, 42, 38, 150}); fillCircle((int)b.x, (int)b.y, (int)b.r, {190, 228, 180, 170}); text(F18, b.l, (int)b.x, (int)b.y - 12, {38, 42, 38, 255}, 1, false); }
        fillCircle(1240, 40, 28, {38, 42, 38, 150}); text(F18, "II", 1240, 28, {255, 255, 255, 255}, 1, false);
    } else {
        text(F18, "A Talk/Use   B Meow   X Hop   Y Rest   ZR Run   + Menu", SW - 20, SH - 30, {255, 255, 255, 220}, 2);
    }
    // dialog
    if (dlgOn) {
        fillRectC(60, SH - 218, SW - 120, 190, INK); fillRectC(66, SH - 212, SW - 132, 178, {244, 238, 222, 255});
        fillRectC(86, SH - 244, 220, 44, INK); fillRectC(90, SH - 240, 212, 36, {110, 140, 105, 255});
        text(F24, dlgName, 196, SH - 236, {255, 255, 255, 255}, 1);
        std::string s = dlg[dlgIdx].substr(0, std::min((size_t)dlgChars, dlg[dlgIdx].size()));
        textWrapped(F32, s, 96, SH - 190, SW - 200, {38, 42, 38, 255});
        if (dlgChars >= dlg[dlgIdx].size()) text(F24, "v", SW - 100, SH - 78 + (int)(std::sin(gameTime * 6) * 3), {110, 140, 105, 255}, 1, false);
    }
    if (fade > 0) fillRectC(0, 0, SW, SH, {10, 14, 30, (Uint8)(255 * clampf(fade, 0, 1))});
}

static const char* menuItems[3] = {"Resume", "Save game", "Quit to title"};
static void drawMenu() {
    fillRectC(0, 0, SW, SH, {20, 28, 22, 150});
    fillRectC(SW / 2 - 203, 190, 406, 300, INK); fillRectC(SW / 2 - 200, 193, 400, 294, {244, 238, 222, 255});
    text(FB30, "Paused", SW / 2, 208, {38, 42, 38, 255}, 1, false);
    for (int i = 0; i < 3; i++) {
        int y = 280 + i * 62;
        if (i == menuSel) fillRectC(SW / 2 - 170, y - 6, 340, 50, {190, 228, 180, 255});
        text(F32, menuItems[i], SW / 2, y, {38, 42, 38, 255}, 1, false);
    }
    text(F18, "Autosaves every 40 seconds", SW / 2, 452, {96, 110, 96, 255}, 1, false);
}

static void drawTitle(float t) {
    for (int i = 0; i < 12; i++) { float k = i / 11.f; fillRectC(0, i * SH / 12, SW, SH / 12 + 1, {(Uint8)lerpf(176, 232, k), (Uint8)lerpf(216, 244, k), (Uint8)lerpf(176, 214, k), 255}); }
    for (int i = 0; i < 18; i++) { float x = std::fmod(i * 173.f + t * 12 * (1 + (i % 3)), SW + 60) - 30, y = 60 + (i * 97) % 520 + std::sin(t + i) * 10; drawSpr(sSpark, x, y, 0.5f + (i % 3) * 0.25f, false, 0.5f, 0.5f, t * 30 + i * 20); }
    for (int i = 0; i < 9; i++) { int x = i * 170 - 20; fillEllipse(x + 60, SH + 40, 150, 130, {132, 178, 120, 255}); fillEllipse(x + 60, SH + 50, 150, 120, {146, 190, 130, 255}); }
    float bob = std::sin(t * 2.4f) * 8;
    drawSpr(sShadow, 360, 600, 2.4f);
    drawSpr(sTitle, 360, 610 + bob * 0.4f, 1.25f, false, 0.5f, 1.f);
    drawSpr(sCrown, 770, 150 + bob, 0.5f);
    text(FB72, TITLE_NAME, 860, 190, {255, 255, 255, 255}, 1);
    text(F24, "a cozy adventure by " STUDIO_NAME, 860, 296, {255, 255, 255, 255}, 1);
    bool has = saveExists();
    const char* items[2] = {has ? "Continue" : "Start", "New Game"}; int n = has ? 2 : 1;
    for (int i = 0; i < n; i++) {
        int y = 360 + i * 70; if (i == titleSel) { fillRectC(710, y - 6, 300, 54, INK); fillRectC(713, y - 3, 294, 48, {244, 238, 222, 255}); }
        text(F32, items[i], 860, y, i == titleSel ? SDL_Color{38, 42, 38, 255} : SDL_Color{255, 255, 255, 255}, 1, i != titleSel);
    }
    text(F18, "Stick / D-pad + A   or tap", 860, 520, {255, 255, 255, 230}, 1);
}

// ------------------------------------------------------------------ main
static void startPlay(bool cont) {
    if (cont) { if (!loadGame()) newGame(); } else newGame();
    flies.clear(); for (int i = 0; i < 40; i++) flies.push_back({200 + frand() * (WORLD_W - 400), 200 + frand() * (WORLD_H - 400), frand() * 6.28f, 20 + frand() * 25});
    mode = M_PLAY; camX = clampf(px - SW / 2, 0, WORLD_W - SW); camY = clampf(py - SH / 2, 0, WORLD_H - SH);
    if (!cont || G.quest[0] == 0) say("Orbitto", {"Mrrow! What a lovely day on the island.", "Walk with the stick, press A to talk to neighbours, and collect the little stars."});
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
#ifdef __SWITCH__
    romfsInit();
#endif
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG); TTF_Init();
    WIN = SDL_CreateWindow(TITLE_NAME, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SW, SH, SDL_WINDOW_SHOWN);
    bool test = getenv("CC_TEST") != nullptr;
    R = SDL_CreateRenderer(WIN, -1, test ? SDL_RENDERER_SOFTWARE : (SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC));
    if (!R) R = SDL_CreateRenderer(WIN, -1, SDL_RENDERER_SOFTWARE);
    if (!R) { fprintf(stderr, "renderer: %s\n", SDL_GetError()); return 1; }
    SDL_RenderSetLogicalSize(R, SW, SH);
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
    SDL_JoystickEventState(SDL_ENABLE); openJoys();
    SDL_AudioSpec want{}, have{}; want.freq = 22050; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 1024; want.callback = audioCb;
    audioDev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0); if (audioDev) SDL_PauseAudioDevice(audioDev, 0);
    auto fp = [](const char* n) { return std::string(ASSET_DIR) + n; };
    F18 = TTF_OpenFont(fp("font.ttf").c_str(), 20); F24 = TTF_OpenFont(fp("font.ttf").c_str(), 26); F32 = TTF_OpenFont(fp("font.ttf").c_str(), 32);
    FB72 = TTF_OpenFont(fp("font_bold.ttf").c_str(), 76); FB30 = TTF_OpenFont(fp("font_bold.ttf").c_str(), 34);
    if (!F18 || !F24 || !F32 || !FB72 || !FB30) { fprintf(stderr, "font load failed\n"); return 1; }
    buildSprites(); buildQuests(); buildWorld(); buildGround();

    Uint32 last = SDL_GetTicks(); float tt = 0; int frame = 0;
    while (!quitReq) {
        Uint32 now = SDL_GetTicks(); float dt = std::min(0.05f, (now - last) / 1000.f); last = now; tt += dt;
        if (test) dt = 1.f / 30.f;
        pollInput();
        if (test) {  // scripted run for headless screenshots
            if (frame == 4 && getenv("CC_HOUR")) G.hour = (float)atof(getenv("CC_HOUR"));
            if (frame == 4 && getenv("CC_TP")) { float a, b; if (sscanf(getenv("CC_TP"), "%f,%f", &a, &b) == 2) { px = a; py = b; camX = clampf(px - SW / 2, 0, WORLD_W - SW); camY = clampf(py - SH / 2, 0, WORLD_H - SH); } }
            if (frame == 30 && getenv("CC_TALK")) { dlgOn = false; talkTo(atoi(getenv("CC_TALK"))); }
            if (frame == 3) in.a = true;
            if (mode == M_PLAY && frame > 6 && frame < 28 && dlgOn) in.a = true;
            if (frame >= 60 && frame < 150 && !getenv("CC_TALK")) in.mx = 1;
            if (frame >= 150 && frame < 210) in.my = 1;
            if (frame == 230) in.b = true;
            if (frame == 260) in.x = true;
            if (frame == 330) in.y = true;
        }
        if (mode == M_TITLE) {
            bool has = saveExists(); int n = has ? 2 : 1;
            if (in.up) { titleSel = (titleSel + n - 1) % n; sfxUi(); } if (in.down) { titleSel = (titleSel + 1) % n; sfxUi(); }
            bool go = in.a;
            if (in.tap) { go = true; if (has) titleSel = in.ty > 400 ? 1 : 0; }
            if (go) { if (has && titleSel == 0) startPlay(true); else startPlay(false); sfxUi(); }
            if (in.plus) quitReq = true;
        } else if (mode == M_PLAY) { updatePlay(dt); updateFx(dt); }
        else if (mode == M_MENU) {
            if (in.up) { menuSel = (menuSel + 2) % 3; sfxUi(); } if (in.down) { menuSel = (menuSel + 1) % 3; sfxUi(); }
            if (in.plus || in.b) { mode = M_PLAY; }
            bool go = in.a;
            if (in.tap) { for (int i = 0; i < 3; i++) if (in.ty > 274 + i * 62 && in.ty < 330 + i * 62) { menuSel = i; go = true; } }
            if (go) {
                if (menuSel == 0) mode = M_PLAY;
                else if (menuSel == 1) { saveGame(); toast("Game saved"); mode = M_PLAY; }
                else { saveGame(); mode = M_TITLE; titleSel = 0; }
            }
        }
        // render
        SDL_SetRenderDrawColor(R, 0, 0, 0, 255); SDL_RenderClear(R);
        if (mode == M_TITLE) drawTitle(tt);
        else { drawWorld(); drawHud(); if (mode == M_MENU) drawMenu(); }
        SDL_RenderPresent(R);
        if (test) {
            const char* out = getenv("CC_OUT"); int shots[] = {2, 20, 45, 120, 200, 245, 280, 360};
            for (int s : shots) if (frame == s && out) {
                SDL_Surface* sf = SDL_CreateRGBSurfaceWithFormat(0, SW, SH, 32, SDL_PIXELFORMAT_ARGB8888);
                SDL_RenderReadPixels(R, nullptr, SDL_PIXELFORMAT_ARGB8888, sf->pixels, sf->pitch);
                char p[512]; snprintf(p, sizeof p, "%s/shot_%03d.bmp", out, frame); SDL_SaveBMP(sf, p); SDL_FreeSurface(sf);
            }
            if (frame == 0 && getenv("CC_TITLE")) { /* title shot handled below */ }
            if (++frame > 365) break;
        }
    }
    if (mode != M_TITLE && !test) saveGame();
    for (auto& p : tcache) SDL_DestroyTexture(p.second.t);
    if (audioDev) SDL_CloseAudioDevice(audioDev);
    TTF_Quit(); IMG_Quit(); SDL_DestroyRenderer(R); SDL_DestroyWindow(WIN); SDL_Quit();
#ifdef __SWITCH__
    romfsExit();
#endif
    return 0;
}
