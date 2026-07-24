#define NOMINMAX
#include <easyx.h>
#include <conio.h>
#include <vector>
#include <algorithm>
#include <cmath>
#include <string>
#include <chrono>
#include <memory>
#include <tchar.h>
#include <windows.h>
#include <unordered_map>
#include <wingdi.h>
#include <mmsystem.h>          // waveOut 函数和 MM 常量

#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "winmm.lib")

// ---------- 常量 ----------
const int SCREEN_W = 800;
const int SCREEN_H = 600;
const float GRAVITY = 980.0f;
const float JUMP_SPEED = -500.0f;
const float MOVE_SPEED = 350.0f;
const float ATTACK_RANGE = 50.0f;
const float ATTACK_DURATION = 0.15f;
const float ATTACK_COOLDOWN = 0.3f;
const float HIT_FLASH_TIME = 0.2f;
const float MAX_VELOCITY_Y = 1000.0f;
const float ENEMY_SPEED = 120.0f;
const float KNOCKBACK_SPEED = 300.0f;
const float KNOCKBACK_DECAY = 800.0f;
const float PLAYER_INVINCIBLE_TIME = 1.0f;
const float ENEMY_KNOCKBACK_TIME = 0.2f;
const int PLAYER_MAX_HP = 50;
const int TOTAL_LEVELS = 5;
#define TRANSPARENT_COLOR RGB(0, 0, 0)

// ---------- 辅助结构 ----------
struct Vector2 {
    float x, y;
    Vector2(float x = 0, float y = 0) : x(x), y(y) {}
    Vector2 operator+(const Vector2& o) const { return { x + o.x, y + o.y }; }
    Vector2 operator-(const Vector2& o) const { return { x - o.x, y - o.y }; }
    Vector2 operator*(float s) const { return { x * s, y * s }; }
    float length() const { return std::sqrt(x * x + y * y); }
    Vector2 normalized() const {
        float l = length();
        if (l < 0.0001f) return { 1, 0 };
        return { x / l, y / l };
    }
};

struct Rect {
    float x, y, w, h;
    Rect(float x = 0, float y = 0, float w = 0, float h = 0) : x(x), y(y), w(w), h(h) {}
    bool intersects(const Rect& other) const {
        return x < other.x + other.w && x + w > other.x &&
            y < other.y + other.h && y + h > other.y;
    }
    Vector2 center() const { return { x + w / 2, y + h / 2 }; }
};

// ---------- 基于 waveOut 的低延迟音效播放器 ----------
class AudioPlayer {
private:
    HWAVEOUT   hWaveOut = NULL;
    WAVEHDR    waveHdr = {};
    LPSTR      pcmData = nullptr;        // PCM 音频数据
    bool       isLoaded = false;

public:
    // 加载 WAV 文件到内存
    bool Load(const wchar_t* filePath) {
        // 使用多媒体 I/O 打开 WAV 文件
        HMMIO hMmio = mmioOpen(const_cast<LPWSTR>(filePath), NULL, MMIO_READ | MMIO_ALLOCBUF);
        if (!hMmio) return false;

        // 查找 'fmt ' 块
        MMCKINFO ckRiff = { 0 }, ckFmt = { 0 };
        ckRiff.fccType = mmioFOURCC('W', 'A', 'V', 'E');
        if (mmioDescend(hMmio, &ckRiff, NULL, MMIO_FINDRIFF) != MMSYSERR_NOERROR) {
            mmioClose(hMmio, 0);
            return false;
        }
        ckFmt.ckid = mmioFOURCC('f', 'm', 't', ' ');
        if (mmioDescend(hMmio, &ckFmt, &ckRiff, MMIO_FINDCHUNK) != MMSYSERR_NOERROR) {
            mmioClose(hMmio, 0);
            return false;
        }

        // 读取 WAVEFORMATEX
        WAVEFORMATEX wfx = {};
        if (mmioRead(hMmio, (HPSTR)&wfx, sizeof(wfx)) != sizeof(wfx)) {
            mmioClose(hMmio, 0);
            return false;
        }
        // 只支持 PCM
        if (wfx.wFormatTag != WAVE_FORMAT_PCM) {
            mmioClose(hMmio, 0);
            return false;
        }

        // 退出 'fmt ' 块
        mmioAscend(hMmio, &ckFmt, 0);

        // 查找 'data' 块
        MMCKINFO ckData = { 0 };
        ckData.ckid = mmioFOURCC('d', 'a', 't', 'a');
        if (mmioDescend(hMmio, &ckData, &ckRiff, MMIO_FINDCHUNK) != MMSYSERR_NOERROR) {
            mmioClose(hMmio, 0);
            return false;
        }

        // 分配内存读取 PCM 数据
        DWORD dataSize = ckData.cksize;
        pcmData = new char[dataSize];
        if (mmioRead(hMmio, pcmData, dataSize) != (LONG)dataSize) {
            delete[] pcmData;
            mmioClose(hMmio, 0);
            return false;
        }
        mmioClose(hMmio, 0);

        // 打开波形输出设备
        if (waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, NULL, NULL, CALLBACK_NULL) != MMSYSERR_NOERROR) {
            delete[] pcmData;
            return false;
        }

        // 准备 WAVEHDR，但先不播放
        ZeroMemory(&waveHdr, sizeof(waveHdr));
        waveHdr.lpData = pcmData;
        waveHdr.dwBufferLength = dataSize;
        waveHdr.dwFlags = 0;
        waveOutPrepareHeader(hWaveOut, &waveHdr, sizeof(waveHdr));

        isLoaded = true;
        return true;
    }

    // 播放音效（可快速重新触发）
    void Play() {
        if (!isLoaded) return;

        // 停止当前播放（如果有），重置播放指针
        waveOutReset(hWaveOut);

        // 重新提交 buffer，从头播放
        waveHdr.dwFlags = 0;               // 清除完成标志
        waveHdr.dwLoops = 0;               // 不循环
        waveOutPrepareHeader(hWaveOut, &waveHdr, sizeof(waveHdr));
        waveOutWrite(hWaveOut, &waveHdr, sizeof(waveHdr));
    }

    ~AudioPlayer() {
        if (isLoaded) {
            waveOutReset(hWaveOut);
            waveOutUnprepareHeader(hWaveOut, &waveHdr, sizeof(waveHdr));
            waveOutClose(hWaveOut);
            delete[] pcmData;
        }
    }
};

// ---------- 全局音效对象 ----------
AudioPlayer attackSound;
AudioPlayer hitSound;

// 初始化音效（在 main 开始时调用）
bool InitAudio() {
    if (!attackSound.Load(L"res/attack.wav")) return false;
    if (!hitSound.Load(L"res/hit.wav")) return false;
    return true;
}

// ---------- BGM 控制（仍用 mciSendString，不需要低延迟）----------
void PlayBGM(const wchar_t* file) {
    static wchar_t cmd[256];
    wsprintf(cmd, L"open \"%s\" type mpegvideo alias bgm", file);
    mciSendString(cmd, NULL, 0, NULL);
    mciSendString(L"play bgm repeat", NULL, 0, NULL);
}

void StopBGM() {
    mciSendString(L"close bgm", NULL, 0, NULL);
}

// ---------- 纹理管理 ----------
class TextureManager {
public:
    static TextureManager& instance() {
        static TextureManager mgr;
        return mgr;
    }
    IMAGE* load(const std::string& name, const wchar_t* file) {
        auto it = textures.find(name);
        if (it != textures.end()) return &it->second;
        IMAGE img;
        loadimage(&img, file);
        if (img.getwidth() == 0) {
            TCHAR msg[256];
            _stprintf_s(msg, _T("加载图片失败: %s\n请确认文件存在且格式正确。"), file);
            MessageBox(NULL, msg, _T("错误"), MB_OK);
            return nullptr;
        }
        textures[name] = img;
        return &textures[name];
    }
    IMAGE* get(const std::string& name) {
        auto it = textures.find(name);
        if (it != textures.end()) return &it->second;
        return nullptr;
    }
private:
    std::unordered_map<std::string, IMAGE> textures;
};

// ---------- 辅助：水平翻转图片 ----------
IMAGE FlipImageHorizontal(IMAGE* src) {
    int w = src->getwidth();
    int h = src->getheight();
    IMAGE dst(w, h);
    DWORD* srcBuf = GetImageBuffer(src);
    DWORD* dstBuf = GetImageBuffer(&dst);
    if (srcBuf && dstBuf) {
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                dstBuf[y * w + x] = srcBuf[y * w + (w - 1 - x)];
            }
        }
    }
    return dst;
}

// ---------- 绘图辅助 ----------
void DrawTexture(IMAGE* tex, int dstX, int dstY, int dstW, int dstH, bool flip = false) {
    if (!tex) return;
    HDC hdc = GetImageHDC(NULL);
    if (!hdc) return;
    if (flip) {
        IMAGE flipped = FlipImageHorizontal(tex);
        HDC srcDC = GetImageHDC(&flipped);
        SetStretchBltMode(hdc, HALFTONE);
        StretchBlt(hdc, dstX, dstY, dstW, dstH, srcDC, 0, 0, flipped.getwidth(), flipped.getheight(), SRCCOPY);
    }
    else {
        HDC srcDC = GetImageHDC(tex);
        SetStretchBltMode(hdc, HALFTONE);
        StretchBlt(hdc, dstX, dstY, dstW, dstH, srcDC, 0, 0, tex->getwidth(), tex->getheight(), SRCCOPY);
    }
}

void DrawTextureTransparent(IMAGE* tex, int dstX, int dstY, int dstW, int dstH, bool flip = false, COLORREF transparentColor = TRANSPARENT_COLOR) {
    if (!tex) return;
    HDC hdc = GetImageHDC(NULL);
    if (!hdc) return;
    if (flip) {
        IMAGE flipped = FlipImageHorizontal(tex);
        HDC srcDC = GetImageHDC(&flipped);
        SetStretchBltMode(hdc, HALFTONE);
        TransparentBlt(hdc, dstX, dstY, dstW, dstH, srcDC, 0, 0, flipped.getwidth(), flipped.getheight(), transparentColor);
    }
    else {
        HDC srcDC = GetImageHDC(tex);
        SetStretchBltMode(hdc, HALFTONE);
        TransparentBlt(hdc, dstX, dstY, dstW, dstH, srcDC, 0, 0, tex->getwidth(), tex->getheight(), transparentColor);
    }
}

void DrawTextureRotatedTransparent(IMAGE* tex, int dstX, int dstY, int dstW, int dstH, double radian, bool flip = false, COLORREF transparentColor = TRANSPARENT_COLOR) {
    if (!tex) return;
    IMAGE srcImg = flip ? FlipImageHorizontal(tex) : *tex;
    IMAGE rotated;
    rotateimage(&rotated, &srcImg, radian, transparentColor, true, true);
    HDC hdc = GetImageHDC(NULL);
    if (!hdc) return;
    HDC srcDC = GetImageHDC(&rotated);
    SetStretchBltMode(hdc, HALFTONE);
    TransparentBlt(hdc, dstX, dstY, dstW, dstH, srcDC, 0, 0, rotated.getwidth(), rotated.getheight(), transparentColor);
}

// ---------- 游戏对象基类 ----------
class GameObject {
public:
    Rect rect;
    Vector2 velocity;
    float dir = 1.0f;
    bool isStatic = false;
    IMAGE* texture = nullptr;
    virtual void update(float dt) {}
    virtual void draw(float offsetX, float offsetY) const = 0;
    virtual ~GameObject() = default;
};

// ---------- 平台 ----------
class Platform : public GameObject {
public:
    Platform(float x, float y, float w, float h, IMAGE* tex = nullptr) {
        rect = { x, y, w, h };
        isStatic = true;
        texture = tex;
    }
    void draw(float offsetX, float offsetY) const override {
        int sx = (int)(rect.x + offsetX), sy = (int)(rect.y + offsetY), sw = (int)rect.w, sh = (int)rect.h;
        if (texture) DrawTextureTransparent(texture, sx, sy, sw, sh, false);
        else { setfillcolor(RGB(80, 160, 80)); fillrectangle(sx, sy, sx + sw, sy + sh); }
    }
};

// ---------- 敌人 ----------
class Enemy : public GameObject {
public:
    int hp, maxHp;
    bool isHit = false;
    float hitTimer = 0.0f;
    Vector2 knockbackVelocity;
    float knockbackTimer = 0.0f;
    float patrolDir = 1.0f;
    int lastAttackedById = 0;

    Enemy(float x, float y, int hp = 3, IMAGE* tex = nullptr) {
        rect = { x, y, 32, 32 };
        this->hp = hp; maxHp = hp; texture = tex;
    }

    void takeDamage(int damage, const Vector2& attackDir, int attackId) {
        if (isHit || hp <= 0 || attackId == lastAttackedById) return;

        // 播放受击音效（低延迟）
        hitSound.Play();

        hp -= damage;
        isHit = true;
        hitTimer = HIT_FLASH_TIME;
        lastAttackedById = attackId;
        Vector2 dirVec = attackDir.length() > 0.001f ? attackDir.normalized() : Vector2(1, 0);
        knockbackVelocity = dirVec * KNOCKBACK_SPEED;
        knockbackVelocity.y = (dirVec.y != 0) ? dirVec.y * 120.0f : -100.0f;
        knockbackTimer = ENEMY_KNOCKBACK_TIME;
    }

    void update(float dt, const std::vector<Platform*>& platforms) {
        if (isHit) { hitTimer -= dt; if (hitTimer <= 0) isHit = false; }
        if (knockbackTimer > 0) {
            knockbackTimer -= dt;
            float decay = KNOCKBACK_DECAY * dt;
            knockbackVelocity.x = knockbackVelocity.x > 0 ? std::max(0.0f, knockbackVelocity.x - decay) : std::min(0.0f, knockbackVelocity.x + decay);
            knockbackVelocity.y = knockbackVelocity.y > 0 ? std::max(0.0f, knockbackVelocity.y - decay) : std::min(0.0f, knockbackVelocity.y + decay);
        }
        velocity.y += GRAVITY * dt;
        if (velocity.y > MAX_VELOCITY_Y) velocity.y = MAX_VELOCITY_Y;
        if (knockbackTimer > 0) { velocity.x = knockbackVelocity.x; velocity.y += knockbackVelocity.y * 0.5f; }
        else { velocity.x = patrolDir * ENEMY_SPEED; }

        rect.x += velocity.x * dt;
        for (auto* plat : platforms) {
            if (rect.intersects(plat->rect)) {
                if (velocity.x > 0) rect.x = plat->rect.x - rect.w;
                else if (velocity.x < 0) rect.x = plat->rect.x + plat->rect.w;
                if (knockbackTimer <= 0) patrolDir *= -1;
                velocity.x = 0;
            }
        }
        rect.y += velocity.y * dt;
        bool onGround = false;
        for (auto* plat : platforms) {
            if (rect.intersects(plat->rect)) {
                if (velocity.y > 0) { rect.y = plat->rect.y - rect.h; velocity.y = 0; onGround = true; }
                else if (velocity.y < 0) { rect.y = plat->rect.y + plat->rect.h; velocity.y = 0; }
            }
        }
        if (rect.y + rect.h > SCREEN_H) { rect.y = SCREEN_H - rect.h; velocity.y = 0; onGround = true; }
        if (knockbackTimer <= 0 && onGround) {
            float checkX = rect.x + (patrolDir > 0 ? rect.w : -1);
            Rect footSensor = { checkX, rect.y + rect.h + 2, 1, 2 };
            bool groundAhead = false;
            for (auto* plat : platforms) { if (footSensor.intersects(plat->rect)) { groundAhead = true; break; } }
            if (!groundAhead) patrolDir *= -1;
        }
        if (rect.x < 0) { rect.x = 0; patrolDir = 1; }
        if (rect.x + rect.w > SCREEN_W) { rect.x = SCREEN_W - rect.w; patrolDir = -1; }
        if (velocity.x != 0) dir = velocity.x > 0 ? 1.0f : -1.0f;
    }

    void draw(float offsetX, float offsetY) const override {
        int sx = (int)(rect.x + offsetX), sy = (int)(rect.y + offsetY), sw = (int)rect.w, sh = (int)rect.h;
        if (isHit && ((int)(hitTimer * 10) % 2 == 0)) return;
        if (texture) DrawTextureTransparent(texture, sx, sy, sw, sh, (dir > 0));
        else { setfillcolor(RGB(255, 80, 80)); fillrectangle(sx, sy, sx + sw, sy + sh); }
    }
};

// ---------- 传送门 ----------
class Portal : public GameObject {
public:
    int targetLevel;
    IMAGE* portalTex = nullptr;
    Portal(float x, float y, int level, IMAGE* tex = nullptr) : targetLevel(level), portalTex(tex) {
        rect = { x, y, 40, 60 }; isStatic = true;
    }
    void draw(float offsetX, float offsetY) const override {
        int sx = (int)(rect.x + offsetX), sy = (int)(rect.y + offsetY);
        if (portalTex) DrawTextureTransparent(portalTex, sx, sy, (int)rect.w, (int)rect.h, false);
        else { setfillcolor(RGB(150, 0, 255)); fillrectangle(sx, sy, sx + (int)rect.w, sy + (int)rect.h); }
    }
};

// ---------- Boss ----------
class Boss : public Enemy {
public:
    float attackCooldown = 0.0f;
    const float BOSS_ATTACK_RANGE = 100.0f, BOSS_ATTACK_SPEED = 400.0f, BOSS_ATTACK_COOLDOWN = 1.5f;
    Vector2 dashVelocity;
    float dashTimer = 0.0f;
    bool isDashing = false;
    Boss(float x, float y, int hp = 20, IMAGE* tex = nullptr) : Enemy(x, y, hp, tex) { rect.w = 60; rect.h = 60; }

    void update(float dt, const std::vector<Platform*>& platforms, const Vector2& playerPos) {
        if (isHit) { hitTimer -= dt; if (hitTimer <= 0) isHit = false; }
        if (knockbackTimer > 0) {
            knockbackTimer -= dt;
            float decay = KNOCKBACK_DECAY * dt;
            knockbackVelocity.x = knockbackVelocity.x > 0 ? std::max(0.0f, knockbackVelocity.x - decay) : std::min(0.0f, knockbackVelocity.x + decay);
            knockbackVelocity.y = knockbackVelocity.y > 0 ? std::max(0.0f, knockbackVelocity.y - decay) : std::min(0.0f, knockbackVelocity.y + decay);
        }
        velocity.y += GRAVITY * dt;
        if (velocity.y > MAX_VELOCITY_Y) velocity.y = MAX_VELOCITY_Y;

        if (isDashing) {
            dashTimer -= dt;
            if (dashTimer <= 0) { isDashing = false; velocity = { 0, 0 }; }
            else { velocity.x = dashVelocity.x; velocity.y = dashVelocity.y; }
        }
        else if (knockbackTimer <= 0) {
            velocity.x = patrolDir * ENEMY_SPEED * 0.8f;
            attackCooldown -= dt;
            float dist = (rect.center() - playerPos).length();
            if (dist < BOSS_ATTACK_RANGE && attackCooldown <= 0) {
                attackCooldown = BOSS_ATTACK_COOLDOWN;
                isDashing = true; dashTimer = 0.4f;
                Vector2 dirVec = (playerPos - rect.center()).normalized();
                dashVelocity = dirVec * BOSS_ATTACK_SPEED;
            }
        }

        rect.x += velocity.x * dt;
        for (auto* plat : platforms) {
            if (rect.intersects(plat->rect)) {
                if (velocity.x > 0) rect.x = plat->rect.x - rect.w;
                else if (velocity.x < 0) rect.x = plat->rect.x + plat->rect.w;
                if (knockbackTimer <= 0 && !isDashing) patrolDir *= -1;
                velocity.x = 0;
            }
        }
        rect.y += velocity.y * dt;
        bool onGround = false;
        for (auto* plat : platforms) {
            if (rect.intersects(plat->rect)) {
                if (velocity.y > 0) { rect.y = plat->rect.y - rect.h; velocity.y = 0; onGround = true; }
                else if (velocity.y < 0) { rect.y = plat->rect.y + plat->rect.h; velocity.y = 0; }
            }
        }
        if (rect.y + rect.h > SCREEN_H) { rect.y = SCREEN_H - rect.h; velocity.y = 0; onGround = true; }
        if (rect.x < 0) rect.x = 0;
        if (rect.x + rect.w > SCREEN_W) rect.x = SCREEN_W - rect.w;
        if (knockbackTimer <= 0 && !isDashing && onGround) {
            float checkX = rect.x + (patrolDir > 0 ? rect.w : -1);
            Rect footSensor = { checkX, rect.y + rect.h + 2, 1, 2 };
            bool groundAhead = false;
            for (auto* plat : platforms) { if (footSensor.intersects(plat->rect)) { groundAhead = true; break; } }
            if (!groundAhead) patrolDir *= -1;
        }
        if (velocity.x != 0) dir = velocity.x > 0 ? 1.0f : -1.0f;
    }

    void draw(float offsetX, float offsetY) const override {
        int sx = (int)(rect.x + offsetX), sy = (int)(rect.y + offsetY);
        if (isHit && ((int)(hitTimer * 10) % 2 == 0)) return;
        if (texture) DrawTextureTransparent(texture, sx, sy, (int)rect.w, (int)rect.h, (dir > 0));
        else { setfillcolor(RGB(255, 20, 20)); fillrectangle(sx, sy, sx + (int)rect.w, sy + (int)rect.h); }
    }
};

// ---------- 玩家 ----------
class Player : public GameObject {
public:
    int hp = PLAYER_MAX_HP, maxHp = PLAYER_MAX_HP;
    bool onGround = false, jumpKeyPressed = false, isAttacking = false;
    float attackTimer = 0.0f, attackCooldown = 0.0f, invincibleTimer = 0.0f;
    Vector2 attackDir = { 1, 0 };
    int attackId = 0;
    IMAGE* attackTex = nullptr;

    Player(float x, float y, IMAGE* tex = nullptr, IMAGE* atkTex = nullptr) {
        rect = { x, y, 30, 40 }; texture = tex; attackTex = atkTex;
    }

    void update(float dt, const std::vector<Platform*>& platforms, std::vector<Enemy*>& enemies) {
        if (invincibleTimer > 0) invincibleTimer -= dt;
        float moveX = 0;
        if (GetAsyncKeyState('A') & 0x8000 || GetAsyncKeyState(VK_LEFT) & 0x8000) moveX -= 1;
        if (GetAsyncKeyState('D') & 0x8000 || GetAsyncKeyState(VK_RIGHT) & 0x8000) moveX += 1;
        velocity.x = moveX * MOVE_SPEED;
        if (moveX != 0) dir = moveX;

        bool jumpDown = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
        if (jumpDown && onGround && !jumpKeyPressed) { velocity.y = JUMP_SPEED; onGround = false; }
        jumpKeyPressed = jumpDown;

        velocity.y += GRAVITY * dt;
        if (velocity.y > MAX_VELOCITY_Y) velocity.y = MAX_VELOCITY_Y;
        if (attackCooldown > 0) attackCooldown -= dt;

        if ((GetAsyncKeyState('J') & 0x8000) && !isAttacking && attackCooldown <= 0) {
            isAttacking = true; attackTimer = ATTACK_DURATION; attackCooldown = ATTACK_COOLDOWN; ++attackId;
            if (GetAsyncKeyState('W') & 0x8000)      attackDir = { 0, -1 };
            else if (GetAsyncKeyState('S') & 0x8000) attackDir = { 0, 1 };
            else if (GetAsyncKeyState('A') & 0x8000) attackDir = { -1, 0 };
            else if (GetAsyncKeyState('D') & 0x8000) attackDir = { 1, 0 };
            else                                     attackDir = { dir, 0 };

            // 播放攻击音效（低延迟）
            attackSound.Play();
        }

        if (isAttacking) {
            attackTimer -= dt;
            if (attackTimer <= 0) { isAttacking = false; }
            else {
                Rect box = getAttackBox();
                for (auto* enemy : enemies) {
                    if (enemy->hp > 0 && box.intersects(enemy->rect))
                        enemy->takeDamage(1, attackDir, attackId);
                }
            }
        }

        rect.x += velocity.x * dt;
        for (auto* plat : platforms) {
            if (rect.intersects(plat->rect)) {
                if (velocity.x > 0) rect.x = plat->rect.x - rect.w;
                else if (velocity.x < 0) rect.x = plat->rect.x + plat->rect.w;
                velocity.x = 0;
            }
        }
        rect.y += velocity.y * dt;
        onGround = false;
        for (auto* plat : platforms) {
            if (rect.intersects(plat->rect)) {
                if (velocity.y > 0) { rect.y = plat->rect.y - rect.h; velocity.y = 0; onGround = true; }
                else if (velocity.y < 0) { rect.y = plat->rect.y + plat->rect.h; velocity.y = 0; }
            }
        }

        if (invincibleTimer <= 0) {
            for (auto* enemy : enemies) {
                if (enemy->hp > 0 && rect.intersects(enemy->rect)) {
                    hp--; invincibleTimer = PLAYER_INVINCIBLE_TIME;
                    Vector2 diff = rect.center() - enemy->rect.center();
                    Vector2 dirVec = diff.length() > 0.0001f ? diff.normalized() : Vector2(1, 0);
                    velocity = dirVec * 300.0f + Vector2(0, -200);
                    onGround = false; rect.x += dirVec.x * 5; rect.y -= 2;
                    break;
                }
            }
        }
        if (rect.y + rect.h > SCREEN_H) { rect.y = SCREEN_H - rect.h; velocity.y = 0; onGround = true; }
        if (rect.x < 0) rect.x = 0;
        if (rect.x + rect.w > SCREEN_W) rect.x = SCREEN_W - rect.w;
    }

    Rect getAttackBox() const {
        Rect box;
        if (std::abs(attackDir.y) > 0.0001f) {
            float offsetY = (attackDir.y < 0) ? -ATTACK_RANGE : rect.h;
            box = { rect.x + 5, rect.y + offsetY, rect.w - 10, ATTACK_RANGE };
        }
        else {
            float offsetX = (attackDir.x > 0) ? rect.w : -ATTACK_RANGE;
            box = { rect.x + offsetX, rect.y + 5, ATTACK_RANGE, rect.h - 10 };
        }
        return box;
    }

    void draw(float offsetX, float offsetY) const override {
        int sx = (int)(rect.x + offsetX), sy = (int)(rect.y + offsetY), sw = (int)rect.w, sh = (int)rect.h;
        bool visible = true;
        if (invincibleTimer > 0 && ((int)(invincibleTimer * 10) % 2 == 0)) visible = false;
        if (visible) {
            if (texture) DrawTextureTransparent(texture, sx, sy, sw, sh, (dir > 0));
            else { setfillcolor(isAttacking ? RGB(100, 200, 255) : RGB(255, 200, 50)); fillrectangle(sx, sy, sx + sw, sy + sh); }
        }
        if (isAttacking) {
            Rect box = getAttackBox();
            int ax = (int)(box.x + offsetX), ay = (int)(box.y + offsetY), aw = (int)box.w, ah = (int)box.h;
            if (attackTex) {
                bool flip = (attackDir.x < 0);
                if (attackDir.y < 0) DrawTextureRotatedTransparent(attackTex, ax, ay, aw, ah, -3.14159265f / 2, false);
                else if (attackDir.y > 0) DrawTextureRotatedTransparent(attackTex, ax, ay, aw, ah, 3.14159265f / 2, false);
                else DrawTextureTransparent(attackTex, ax, ay, aw, ah, flip);
            }
            else { setlinecolor(RGB(255, 255, 0)); rectangle(ax, ay, ax + aw, ay + ah); }
        }
    }
};

// ---------- 相机 ----------
class Camera {
public:
    Vector2 position;
    void follow(const Vector2& target, float dt) {
        position.x += (target.x - position.x) * std::min(1.0f, 5.0f * dt);
        position.y += (target.y - position.y) * std::min(1.0f, 5.0f * dt);
    }
};

// ---------- 游戏状态 ----------
enum class GameState { MENU, PLAYING, GAMEOVER, WIN };

// ---------- 游戏世界 ----------
class GameWorld {
public:
    std::unique_ptr<Player> playerPtr;
    Player* player = nullptr;
    std::vector<std::unique_ptr<Enemy>> enemies;
    std::vector<std::unique_ptr<Platform>> platforms;
    std::vector<std::unique_ptr<Portal>> portals;
    std::unique_ptr<Boss> boss;
    Camera camera;
    int currentLevel = 0;
    Vector2 playerStartPos;
    IMAGE* playerTex = nullptr, * enemyTex = nullptr, * platformTex = nullptr, * attackTex = nullptr, * backgroundTex = nullptr, * portalTex = nullptr, * bossTex = nullptr;

    void loadLevel(int level) {
        currentLevel = level;
        platforms.clear(); enemies.clear(); portals.clear(); boss.reset();
        auto& tm = TextureManager::instance();
        playerTex = tm.get("player"); enemyTex = tm.get("enemy"); platformTex = tm.get("platform");
        attackTex = tm.get("attack"); backgroundTex = tm.get("background"); portalTex = tm.get("portal"); bossTex = tm.get("boss");
        int savedHp = player ? player->hp : PLAYER_MAX_HP;
        playerPtr = std::make_unique<Player>(100, 300, playerTex, attackTex);
        player = playerPtr.get(); player->hp = savedHp;
        buildLevel(level);
    }

    void buildLevel(int level) {
        switch (level) {
        case 0:
            playerStartPos = { 50, 400 }; player->rect.x = playerStartPos.x; player->rect.y = playerStartPos.y;
            platforms.push_back(std::make_unique<Platform>(0, 550, 800, 30, platformTex));
            platforms.push_back(std::make_unique<Platform>(200, 450, 150, 20, platformTex));
            platforms.push_back(std::make_unique<Platform>(500, 380, 200, 20, platformTex));
            enemies.push_back(std::make_unique<Enemy>(400, 500, 3, enemyTex));
            portals.push_back(std::make_unique<Portal>(700, 500, 1, portalTex)); break;
        case 1:
            playerStartPos = { 50, 500 }; player->rect.x = playerStartPos.x; player->rect.y = playerStartPos.y;
            platforms.push_back(std::make_unique<Platform>(0, 550, 800, 30, platformTex));
            platforms.push_back(std::make_unique<Platform>(100, 440, 150, 20, platformTex));
            platforms.push_back(std::make_unique<Platform>(400, 330, 250, 20, platformTex));
            enemies.push_back(std::make_unique<Enemy>(500, 280, 4, enemyTex));
            enemies.push_back(std::make_unique<Enemy>(600, 450, 4, enemyTex));
            portals.push_back(std::make_unique<Portal>(700, 200, 2, portalTex)); break;
        case 2:
            playerStartPos = { 100, 400 }; player->rect.x = playerStartPos.x; player->rect.y = playerStartPos.y;
            platforms.push_back(std::make_unique<Platform>(0, 550, 800, 30, platformTex));
            platforms.push_back(std::make_unique<Platform>(50, 440, 120, 20, platformTex));
            platforms.push_back(std::make_unique<Platform>(300, 330, 200, 20, platformTex));
            enemies.push_back(std::make_unique<Enemy>(200, 500, 5, enemyTex));
            portals.push_back(std::make_unique<Portal>(150, 350, 3, portalTex)); break;
        case 3:
            playerStartPos = { 50, 500 }; player->rect.x = playerStartPos.x; player->rect.y = playerStartPos.y;
            platforms.push_back(std::make_unique<Platform>(0, 550, 800, 30, platformTex));
            platforms.push_back(std::make_unique<Platform>(600, 440, 150, 20, platformTex));
            enemies.push_back(std::make_unique<Enemy>(100, 500, 4, enemyTex));
            enemies.push_back(std::make_unique<Enemy>(400, 450, 6, enemyTex));
            portals.push_back(std::make_unique<Portal>(750, 500, 4, portalTex)); break;
        case 4:
            playerStartPos = { 100, 300 }; player->rect.x = playerStartPos.x; player->rect.y = playerStartPos.y;
            platforms.push_back(std::make_unique<Platform>(0, 550, 800, 30, platformTex));
            platforms.push_back(std::make_unique<Platform>(300, 430, 200, 20, platformTex));
            boss = std::make_unique<Boss>(500, 400, 25, bossTex); break;
        }
    }

    GameState update(float dt) {
        std::vector<Platform*> platPtrs;
        for (auto& p : platforms) platPtrs.push_back(p.get());
        std::vector<Enemy*> enemyPtrs;
        for (auto& e : enemies) enemyPtrs.push_back(e.get());

        for (auto& e : enemies) e->update(dt, platPtrs);
        if (boss && boss->hp > 0) {
            boss->update(dt, platPtrs, player->rect.center());
            if (player->invincibleTimer <= 0 && boss->rect.intersects(player->rect)) {
                player->hp--; player->invincibleTimer = PLAYER_INVINCIBLE_TIME;
                Vector2 diff = player->rect.center() - boss->rect.center();
                Vector2 dirVec = diff.length() > 0.0001f ? diff.normalized() : Vector2(1, 0);
                player->velocity = dirVec * 300.0f + Vector2(0, -200); player->onGround = false;
            }
        }
        player->update(dt, platPtrs, enemyPtrs);
        if (player->isAttacking && boss && boss->hp > 0) {
            if (player->getAttackBox().intersects(boss->rect))
                boss->takeDamage(1, player->attackDir, player->attackId);
        }
        for (auto& portal : portals) {
            if (player->rect.intersects(portal->rect) && (GetAsyncKeyState('W') & 0x8000 || GetAsyncKeyState(VK_UP) & 0x8000)) {
                if (portal->targetLevel < TOTAL_LEVELS) { loadLevel(portal->targetLevel); return GameState::PLAYING; }
            }
        }
        enemies.erase(std::remove_if(enemies.begin(), enemies.end(), [](const std::unique_ptr<Enemy>& e) { return e->hp <= 0; }), enemies.end());
        if (boss && boss->hp <= 0 && currentLevel == TOTAL_LEVELS - 1) { boss.reset(); return GameState::WIN; }
        if (player->hp <= 0) return GameState::GAMEOVER;
        camera.follow(player->rect.center(), dt);
        return GameState::PLAYING;
    }

    void draw() {
        if (backgroundTex) DrawTexture(backgroundTex, 0, 0, SCREEN_W, SCREEN_H);
        else { setbkcolor(RGB(40, 40, 60)); cleardevice(); }
        float ox = -camera.position.x + SCREEN_W / 2, oy = -camera.position.y + SCREEN_H / 2;
        for (auto& p : platforms) p->draw(ox, oy);
        for (auto& e : enemies) e->draw(ox, oy);
        for (auto& portal : portals) portal->draw(ox, oy);
        if (boss && boss->hp > 0) boss->draw(ox, oy);
        player->draw(ox, oy);

        settextcolor(RGB(255, 255, 255));
        TCHAR buf[128]; _stprintf_s(buf, _T("HP: %d/%d  层数: %d/%d  敌人: %d"), player->hp, player->maxHp, currentLevel + 1, TOTAL_LEVELS, (int)enemies.size());
        outtextxy(10, 10, buf);

        if (boss && boss->hp > 0) {
            setfillcolor(RGB(80, 0, 0)); fillrectangle(200, 560, 600, 580);
            setfillcolor(RGB(255, 0, 0));
            float ratio = (float)boss->hp / boss->maxHp;
            fillrectangle(200, 560, 200 + (int)(400 * ratio), 580);
            settextcolor(RGB(255, 255, 255));
            TCHAR bbuf[32]; _stprintf_s(bbuf, _T("Boss %d/%d"), boss->hp, boss->maxHp);
            outtextxy(350, 562, bbuf);
        }
    }
};

// ---------- 菜单 ----------
void drawMenu(int selected) {
    const TCHAR* items[] = { _T("开始游戏"), _T("退出") };
    RECT rects[2];
    for (int i = 0; i < 2; i++) { rects[i].left = SCREEN_W / 2 - 100; rects[i].right = SCREEN_W / 2 + 100; rects[i].top = 300 + i * 60; rects[i].bottom = 330 + i * 60; }
    setbkcolor(RGB(30, 30, 50)); cleardevice();
    settextcolor(RGB(255, 215, 0)); settextstyle(60, 0, _T("微软雅黑")); outtextxy(SCREEN_W / 2 - 180, 150, _T("类银 Demo"));
    settextstyle(30, 0, _T("微软雅黑"));
    for (int i = 0; i < 2; i++) {
        if (i == selected) { settextcolor(RGB(255, 255, 100)); setfillcolor(RGB(80, 80, 120)); fillrectangle(rects[i].left, rects[i].top, rects[i].right, rects[i].bottom); }
        else { settextcolor(RGB(200, 200, 200)); }
        outtextxy(rects[i].left + 20, rects[i].top + 5, items[i]);
    }
    settextstyle(20, 0, _T("微软雅黑")); settextcolor(RGB(150, 150, 150)); outtextxy(SCREEN_W / 2 - 130, 500, _T("鼠标点击"));
}

GameState menuLoop() {
    int selected = 0; bool lastLeft = false;
    RECT rects[2] = { { SCREEN_W / 2 - 100, 300, SCREEN_W / 2 + 100, 330 }, { SCREEN_W / 2 - 100, 360, SCREEN_W / 2 + 100, 390 } };
    while (true) {
        BeginBatchDraw(); drawMenu(selected); EndBatchDraw();
        if (_kbhit()) {
            int key = _getch();
            if (key == 224) { key = _getch(); if (key == 72) selected = (selected - 1 + 2) % 2; else if (key == 80) selected = (selected + 1) % 2; }
            else if (key == 13) { return selected == 0 ? GameState::PLAYING : GameState::GAMEOVER; }
        }
        bool leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (leftDown && !lastLeft) {
            POINT pt; GetCursorPos(&pt); HWND hwnd = GetHWnd(); ScreenToClient(hwnd, &pt);
            for (int i = 0; i < 2; i++) { if (pt.x >= rects[i].left && pt.x <= rects[i].right && pt.y >= rects[i].top && pt.y <= rects[i].bottom) return i == 0 ? GameState::PLAYING : GameState::GAMEOVER; }
        }
        lastLeft = leftDown; Sleep(30);
    }
}

void drawEndScreen(const TCHAR* text) {
    // 1. 清空之前的按键残留，防止误触
    while (_kbhit()) _getch();

    // 2. 循环等待指定按键
    bool exitLoop = false;
    while (!exitLoop) {
        // 检测回车键是否按下
        if (GetAsyncKeyState(VK_RETURN) & 0x8000) {
            exitLoop = true;
            // 等待按键释放，避免按住回车直接跳过下一画面
            while (GetAsyncKeyState(VK_RETURN) & 0x8000) Sleep(10);
        }

        // 3. 刷新画面
        BeginBatchDraw();
        setbkcolor(RGB(0, 0, 0));
        cleardevice();
        settextcolor(RGB(255, 0, 0));
        settextstyle(40, 0, _T("微软雅黑"));
        outtextxy(SCREEN_W / 2 - 100, SCREEN_H / 2 - 20, text);
        settextstyle(20, 0, _T("微软雅黑"));
        outtextxy(SCREEN_W / 2 - 100, SCREEN_H / 2 + 40, _T("按 回车键 返回菜单"));
        EndBatchDraw();
        Sleep(30);
    }

    // 4. 再次清空缓冲，防止残留干扰
    while (_kbhit()) _getch();
}
// ---------- 主函数 ----------
int main() {
    initgraph(SCREEN_W, SCREEN_H);

    // 加载纹理
    auto& tm = TextureManager::instance();
    tm.load("player", L"res/player.png");
    tm.load("enemy", L"res/enemy.png");
    tm.load("platform", L"res/platform.png");
    tm.load("attack", L"res/attack.png");
    tm.load("background", L"res/background.png");
    tm.load("portal", L"res/portal.png");
    tm.load("boss", L"res/boss.png");

    // 初始化音效（必须使用 PCM WAV 格式）
    if (!InitAudio()) {
        MessageBox(NULL, _T("音效加载失败，请检查 res/attack.wav 和 res/hit.wav 是否为 PCM WAV 格式"), _T("错误"), MB_OK);
    }

    // 播放背景音乐（使用 mciSendString，可播放 MP3）
    PlayBGM(L"res/bgm.mp3");
    mciSendString(L"setaudio bgm volume to 100", NULL, 0, NULL);

    GameWorld world;
    GameState state = GameState::MENU;
    while (state != GameState::GAMEOVER) {
        if (state == GameState::MENU) {
            state = menuLoop();
            if (state == GameState::PLAYING) world.loadLevel(0);
        }
        else if (state == GameState::PLAYING || state == GameState::WIN) {
            // --- 固定时间步长相关变量 ---
            const float FIXED_DT = 1.0f / 60.0f;
            float accumulator = 0.0f;
            auto lastTime = std::chrono::steady_clock::now();

            while (state == GameState::PLAYING) {
                auto now = std::chrono::steady_clock::now();
                float frameTime = std::chrono::duration<float>(now - lastTime).count();
                lastTime = now;

                // 防止螺旋叠加，限制单帧最大时间
                if (frameTime > 0.25f) frameTime = 0.25f;
                accumulator += frameTime;

                // 固定步长更新物理
                while (accumulator >= FIXED_DT) {
                    state = world.update(FIXED_DT);
                    accumulator -= FIXED_DT;
                }

                // 按 ESC 返回菜单
                if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
                    state = GameState::MENU;
                    break;
                }

                // 渲染
                BeginBatchDraw();
                world.draw();
                EndBatchDraw();

                Sleep(1);
            }

            if (state == GameState::WIN) {
                drawEndScreen(_T("你赢了!"));
                state = GameState::MENU;
            }
            else if (state == GameState::GAMEOVER) {
                drawEndScreen(_T("游戏结束"));
                state = GameState::MENU;
            }
        }
    }

    StopBGM();
    closegraph();
    return 0;
}