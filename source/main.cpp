#define TESLA_INIT_IMPL
#define TESLA_PRESERVE_OVERLAY_ON_HOME
#define TESLA_IGNORE_TOUCH_HIDE
#include <tesla.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>

namespace {

constexpr char CONFIG_DIR[] = "sdmc:/config/gamepad-overlay";
constexpr char CONFIG_FILE[] = "sdmc:/config/gamepad-overlay/config.ini";

constexpr float kBaseWidth = 180.0F;
constexpr float kBaseHeight = 128.0F;
constexpr float kMargin = 24.0F;
constexpr u8 kBackdropAlpha = 3;
constexpr char TESLA_MENU_PATH[] = "sdmc:/switch/.overlays/ovlmenu.ovl";

constexpr tsl::Color kDefaultAccent = { 0xB, 0x6, 0xF, 0xF };
constexpr tsl::Color kWhite = { 0xF, 0xF, 0xF, 0xF };

constexpr std::array<float, 7> kScaleValues = {
    0.50F, 0.75F, 1.00F, 1.25F, 1.50F, 1.75F, 2.00F,
};

constexpr std::array<const char*, 7> kScaleLabels = {
    "0.50x", "0.75x", "1.00x", "1.25x", "1.50x", "1.75x", "2.00x",
};

constexpr std::array<float, 7> kStickScaleValues = {
    0.75F, 1.00F, 1.25F, 1.50F, 1.75F, 2.00F, 2.25F,
};

constexpr std::array<const char*, 7> kStickScaleLabels = {
    "0.75x", "1.00x", "1.25x", "1.50x", "1.75x", "2.00x", "2.25x",
};

constexpr std::array<float, 7> kStickTravelValues = {
    0.75F, 1.00F, 1.25F, 1.50F, 1.75F, 2.00F, 2.25F,
};

constexpr std::array<const char*, 7> kStickTravelLabels = {
    "0.75x", "1.00x", "1.25x", "1.50x", "1.75x", "2.00x", "2.25x",
};

struct OverlayConfig {
    enum class Corner {
        TopLeft,
        BottomLeft,
    };

    Corner corner = Corner::BottomLeft;
    tsl::Color accent = kDefaultAccent;
    float scale = 1.0F;
    float stickScale = 1.0F;
    float stickTravel = 1.0F;
    bool backgroundPanel = true;
};

struct PreviewState {
    bool connected = false;
    bool handheld = false;
    u64 buttons = 0;
    HidAnalogStickState leftStick = { 0, 0 };
    HidAnalogStickState rightStick = { 0, 0 };
};

class ConfigGui;
class HudGui;
class GamepadOverlay;

std::string trim(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return "";

    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

int hexValue(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F')
        return 10 + (c - 'A');
    return -1;
}

u8 nibbleToByte(u8 nibble) {
    return static_cast<u8>((nibble << 4) | nibble);
}

bool parseColor(const std::string& rawValue, tsl::Color& outColor) {
    std::string value = trim(rawValue);
    if (!value.empty() && value.front() == '#')
        value.erase(value.begin());

    if (value.size() != 6 && value.size() != 8)
        return false;

    std::array<int, 8> digits = { 0, 0, 0, 0, 0, 0, 0, 0 };
    for (size_t i = 0; i < value.size(); i++) {
        digits[i] = hexValue(value[i]);
        if (digits[i] < 0)
            return false;
    }

    outColor.r = static_cast<u8>((digits[0] << 4 | digits[1]) >> 4);
    outColor.g = static_cast<u8>((digits[2] << 4 | digits[3]) >> 4);
    outColor.b = static_cast<u8>((digits[4] << 4 | digits[5]) >> 4);
    outColor.a = value.size() == 8
        ? static_cast<u8>((digits[6] << 4 | digits[7]) >> 4)
        : 0xF;
    return true;
}

OverlayConfig::Corner parseCorner(const std::string& rawValue) {
    const std::string value = toLower(trim(rawValue));
    if (value == "top-left")
        return OverlayConfig::Corner::TopLeft;
    return OverlayConfig::Corner::BottomLeft;
}

const char* cornerToString(OverlayConfig::Corner corner) {
    switch (corner) {
        case OverlayConfig::Corner::TopLeft:
            return "Left Up";
        case OverlayConfig::Corner::BottomLeft:
            return "Left Down";
        default:
            return "Left Down";
    }
}

size_t getScaleIndex(float scale) {
    size_t bestIndex = 0;
    float bestDistance = std::abs(scale - kScaleValues[0]);
    for (size_t i = 1; i < kScaleValues.size(); i++) {
        const float distance = std::abs(scale - kScaleValues[i]);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIndex = i;
        }
    }
    return bestIndex;
}

template <size_t N>
size_t getNearestIndex(float value, const std::array<float, N>& values) {
    size_t bestIndex = 0;
    float bestDistance = std::abs(value - values[0]);
    for (size_t i = 1; i < values.size(); i++) {
        const float distance = std::abs(value - values[i]);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIndex = i;
        }
    }
    return bestIndex;
}

std::string formatColorHex(const tsl::Color& color) {
    char buffer[16] = { 0 };
    std::snprintf(
        buffer,
        sizeof(buffer),
        "#%02X%02X%02X",
        nibbleToByte(color.r),
        nibbleToByte(color.g),
        nibbleToByte(color.b)
    );
    return buffer;
}

bool parseBool(const std::string& rawValue, bool defaultValue) {
    const std::string value = toLower(trim(rawValue));
    if (value == "1" || value == "true" || value == "on" || value == "yes")
        return true;
    if (value == "0" || value == "false" || value == "off" || value == "no")
        return false;
    return defaultValue;
}

OverlayConfig loadConfig() {
    OverlayConfig config;

    tsl::hlp::doWithSDCardHandle([&config]() {
        FILE* file = fopen(CONFIG_FILE, "rb");
        if (!file)
            return;

        char lineBuffer[160] = { 0 };
        while (fgets(lineBuffer, sizeof(lineBuffer), file) != nullptr) {
            std::string line = trim(lineBuffer);
            if (line.empty() || line[0] == '#' || line[0] == ';')
                continue;

            const auto split = line.find('=');
            if (split == std::string::npos)
                continue;

            const std::string key = toLower(trim(line.substr(0, split)));
            const std::string value = trim(line.substr(split + 1));

            if (key == "corner") {
                config.corner = parseCorner(value);
            } else if (key == "accent" || key == "pressed_color") {
                tsl::Color parsed = config.accent;
                if (parseColor(value, parsed))
                    config.accent = parsed;
            } else if (key == "scale") {
                const float scale = std::strtof(value.c_str(), nullptr);
                if (scale > 0.0F)
                    config.scale = std::clamp(scale, 0.5F, 2.0F);
            } else if (key == "stick_scale") {
                const float stickScale = std::strtof(value.c_str(), nullptr);
                if (stickScale > 0.0F)
                    config.stickScale = std::clamp(stickScale, 0.75F, 2.25F);
            } else if (key == "stick_travel") {
                const float stickTravel = std::strtof(value.c_str(), nullptr);
                if (stickTravel > 0.0F)
                    config.stickTravel = std::clamp(stickTravel, 0.75F, 2.25F);
            } else if (key == "background_panel") {
                config.backgroundPanel = parseBool(value, config.backgroundPanel);
            }
        }

        fclose(file);
    });

    return config;
}

void saveConfig(const OverlayConfig& config) {
    tsl::hlp::doWithSDCardHandle([&config]() {
        mkdir("sdmc:/config", 0755);
        mkdir(CONFIG_DIR, 0755);

        FILE* file = fopen(CONFIG_FILE, "wb");
        if (!file)
            return;

        std::string output;
        output += "corner=";
        switch (config.corner) {
            case OverlayConfig::Corner::TopLeft:
                output += "top-left\n";
                break;
            case OverlayConfig::Corner::BottomLeft:
                output += "bottom-left\n";
                break;
        }

        char line[64] = { 0 };
        std::snprintf(line, sizeof(line), "accent=%s\n", formatColorHex(config.accent).c_str());
        output += line;
        std::snprintf(line, sizeof(line), "scale=%.2f\n", config.scale);
        output += line;
        std::snprintf(line, sizeof(line), "stick_scale=%.2f\n", config.stickScale);
        output += line;
        std::snprintf(line, sizeof(line), "stick_travel=%.2f\n", config.stickTravel);
        output += line;
        std::snprintf(line, sizeof(line), "background_panel=%s\n", config.backgroundPanel ? "on" : "off");
        output += line;

        fwrite(output.c_str(), 1, output.size(), file);
        fclose(file);
    });
}

void drawRoundedRect(tsl::gfx::Renderer* renderer, s32 x, s32 y, s32 w, s32 h, s32 radius, tsl::Color color) {
    radius = std::max(0, std::min(radius, std::min(w / 2, h / 2)));
    if (radius == 0) {
        renderer->drawRect(x, y, w, h, color);
        return;
    }

    renderer->drawRect(x + radius, y, w - radius * 2, h, color);
    renderer->drawRect(x, y + radius, radius, h - radius * 2, color);
    renderer->drawRect(x + w - radius, y + radius, radius, h - radius * 2, color);

    renderer->drawCircle(x + radius, y + radius, radius, true, color);
    renderer->drawCircle(x + w - radius, y + radius, radius, true, color);
    renderer->drawCircle(x + radius, y + h - radius, radius, true, color);
    renderer->drawCircle(x + w - radius, y + h - radius, radius, true, color);
}

void drawBackdropPanel(tsl::gfx::Renderer* renderer, s32 x, s32 y, s32 w, s32 h, tsl::Color color) {
    renderer->drawRect(x, y, w, h, color);
}

void drawCircleButton(tsl::gfx::Renderer* renderer, s32 cx, s32 cy, s32 radius, bool pressed, tsl::Color accent, tsl::Color outline, tsl::Color fill) {
    const tsl::Color buttonFill = pressed ? accent : fill;
    renderer->drawCircle(cx, cy, radius, true, buttonFill);
    renderer->drawCircle(cx, cy, radius, false, outline);
    renderer->drawCircle(cx, cy, std::max(1, radius - 1), false, outline);
}

void drawStick(tsl::gfx::Renderer* renderer, s32 cx, s32 cy, s32 radius, bool pressed, HidAnalogStickState state, float sizeScale, float travelScale, tsl::Color accent, tsl::Color outline, tsl::Color fill) {
    const s32 outerRadius = std::max(4, static_cast<s32>(std::lround(static_cast<float>(radius) * sizeScale)));
    const s32 nubRadius = std::max(3, static_cast<s32>(std::lround(static_cast<float>(radius) * 0.5F * sizeScale)));

    renderer->drawCircle(cx, cy, outerRadius, true, fill);
    renderer->drawCircle(cx, cy, outerRadius, false, outline);
    renderer->drawCircle(cx, cy, std::max(1, outerRadius - 1), false, outline);

    if (pressed) {
        renderer->drawCircle(cx, cy, outerRadius + 3, false, accent);
        renderer->drawCircle(cx, cy, outerRadius + 4, false, accent);
    }

    const float offsetRange = static_cast<float>(outerRadius) * 0.45F * travelScale;
    const float nx = std::clamp(static_cast<float>(state.x) / JOYSTICK_MAX, -1.0F, 1.0F);
    const float ny = std::clamp(static_cast<float>(state.y) / JOYSTICK_MAX, -1.0F, 1.0F);
    const s32 nubX = cx + static_cast<s32>(std::lround(nx * offsetRange));
    const s32 nubY = cy - static_cast<s32>(std::lround(ny * offsetRange));

    renderer->drawCircle(nubX, nubY, nubRadius, true, accent);
    renderer->drawCircle(nubX, nubY, nubRadius, false, outline);
}

void drawPlusMinus(tsl::gfx::Renderer* renderer, s32 x, s32 y, s32 w, s32 h, bool pressed, bool plus, tsl::Color accent, tsl::Color outline, tsl::Color fill) {
    const tsl::Color buttonFill = pressed ? accent : fill;
    drawRoundedRect(renderer, x, y, w, h, h / 2, outline);
    drawRoundedRect(renderer, x + 1, y + 1, std::max(1, w - 2), std::max(1, h - 2), std::max(1, (h - 2) / 2), buttonFill);

    const s32 lineThickness = std::max(1, h / 4);
    const s32 lineY = y + h / 2 - lineThickness / 2;
    renderer->drawRect(x + w / 4, lineY, w / 2, lineThickness, kWhite);

    if (plus) {
        const s32 lineX = x + w / 2 - lineThickness / 2;
        renderer->drawRect(lineX, y + h / 4, lineThickness, h / 2, kWhite);
    }
}

void drawShoulder(tsl::gfx::Renderer* renderer, s32 x, s32 y, s32 w, s32 h, bool pressed, tsl::Color accent, tsl::Color outline, tsl::Color fill) {
    const tsl::Color buttonFill = pressed ? accent : fill;
    drawRoundedRect(renderer, x, y, w, h, std::max(1, h / 2), outline);
    drawRoundedRect(renderer, x + 1, y + 1, std::max(1, w - 2), std::max(1, h - 2), std::max(1, (h - 2) / 2), buttonFill);
}

void drawGamepad(tsl::gfx::Renderer* renderer, const PreviewState& state, const OverlayConfig& config, s32 x, s32 y, float scale) {
    const s32 width = static_cast<s32>(std::lround(kBaseWidth * scale));
    const tsl::Color outline = { 0xF, 0xF, 0xF, static_cast<u8>(state.connected ? 0xE : 0x6) };
    const tsl::Color fill = { 0xF, 0xF, 0xF, static_cast<u8>(state.connected ? 0x4 : 0x2) };
    tsl::Color accent = config.accent;

    if (!state.connected)
        accent.a = std::min<u8>(accent.a, 0x8);

    const auto scalePx = [scale](s32 value) -> s32 {
        return static_cast<s32>(std::lround(static_cast<float>(value) * scale));
    };
    const auto pressed = [&state](u64 button) -> bool {
        return (state.buttons & button) != 0;
    };

    drawShoulder(renderer, x + scalePx(8), y + scalePx(4), scalePx(22), scalePx(6), pressed(HidNpadButton_ZL), accent, outline, fill);
    drawShoulder(renderer, x + scalePx(10), y + scalePx(14), scalePx(18), scalePx(6), pressed(HidNpadButton_L), accent, outline, fill);
    drawShoulder(renderer, x + width - scalePx(30), y + scalePx(4), scalePx(22), scalePx(6), pressed(HidNpadButton_ZR), accent, outline, fill);
    drawShoulder(renderer, x + width - scalePx(28), y + scalePx(14), scalePx(18), scalePx(6), pressed(HidNpadButton_R), accent, outline, fill);

    drawPlusMinus(renderer, x + scalePx(69), y + scalePx(18), scalePx(16), scalePx(8), pressed(HidNpadButton_Minus), false, accent, outline, fill);
    drawPlusMinus(renderer, x + scalePx(95), y + scalePx(18), scalePx(16), scalePx(8), pressed(HidNpadButton_Plus), true, accent, outline, fill);

    drawStick(renderer, x + scalePx(44), y + scalePx(56), scalePx(13), pressed(HidNpadButton_StickL), state.leftStick, config.stickScale, config.stickTravel, accent, outline, fill);
    drawStick(renderer, x + scalePx(136), y + scalePx(96), scalePx(13), pressed(HidNpadButton_StickR), state.rightStick, config.stickScale, config.stickTravel, accent, outline, fill);

    const s32 dpadCx = x + scalePx(44);
    const s32 dpadCy = y + scalePx(96);
    const s32 dpadOffset = scalePx(13);
    const s32 buttonRadius = scalePx(7);
    drawCircleButton(renderer, dpadCx, dpadCy - dpadOffset, buttonRadius, pressed(HidNpadButton_Up), accent, outline, fill);
    drawCircleButton(renderer, dpadCx - dpadOffset, dpadCy, buttonRadius, pressed(HidNpadButton_Left), accent, outline, fill);
    drawCircleButton(renderer, dpadCx + dpadOffset, dpadCy, buttonRadius, pressed(HidNpadButton_Right), accent, outline, fill);
    drawCircleButton(renderer, dpadCx, dpadCy + dpadOffset, buttonRadius, pressed(HidNpadButton_Down), accent, outline, fill);

    const s32 faceCx = x + scalePx(136);
    const s32 faceCy = y + scalePx(56);
    const s32 faceOffset = scalePx(13);
    drawCircleButton(renderer, faceCx, faceCy - faceOffset, buttonRadius, pressed(HidNpadButton_X), accent, outline, fill);
    drawCircleButton(renderer, faceCx - faceOffset, faceCy, buttonRadius, pressed(HidNpadButton_Y), accent, outline, fill);
    drawCircleButton(renderer, faceCx + faceOffset, faceCy, buttonRadius, pressed(HidNpadButton_A), accent, outline, fill);
    drawCircleButton(renderer, faceCx, faceCy + faceOffset, buttonRadius, pressed(HidNpadButton_B), accent, outline, fill);

}

struct SharedState {
    OverlayConfig config;
    PreviewState preview;
    PadState pad = {};

    void init() {
        this->config = loadConfig();
        padInitialize(&this->pad, HidNpadIdType_No1, HidNpadIdType_Handheld, HidNpadIdType_Other);
        this->updatePreview();
    }

    void updatePreview() {
        padUpdate(&this->pad);
        this->preview.connected = padIsConnected(&this->pad);
        this->preview.handheld = padIsHandheld(&this->pad);
        this->preview.buttons = padGetButtons(&this->pad);
        this->preview.leftStick = padGetStickPos(&this->pad, 0);
        this->preview.rightStick = padGetStickPos(&this->pad, 1);
    }

    void persist() const {
        saveConfig(this->config);
    }

    void resetDefaults() {
        this->config = OverlayConfig{};
        this->persist();
    }
};

class HudCanvas : public tsl::elm::Element {
public:
    virtual void draw(tsl::gfx::Renderer* renderer) override;

    virtual void layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override {
        this->setBoundaries(parentX, parentY, parentWidth, parentHeight);
    }

    virtual tsl::elm::Element* requestFocus(tsl::elm::Element*, tsl::FocusDirection) override {
        return nullptr;
    }
};

class ConfigGui : public tsl::Gui {
public:
    ConfigGui() = default;

    virtual tsl::elm::Element* createUI() override;
    virtual void update() override;
    virtual bool handleInput(u64 keysDown, u64, const HidTouchState&, HidAnalogStickState, HidAnalogStickState) override;

private:
    void syncWidgets();
    void cycleCorner(int delta);

    tsl::elm::ListItem* m_cornerItem = nullptr;
    tsl::elm::ListItem* m_stickScaleItem = nullptr;
    tsl::elm::ListItem* m_stickTravelItem = nullptr;
    tsl::elm::ListItem* m_colorValueItem = nullptr;
    tsl::elm::ToggleListItem* m_panelToggle = nullptr;
    tsl::elm::NamedStepTrackBar* m_scaleBar = nullptr;
    tsl::elm::NamedStepTrackBar* m_stickScaleBar = nullptr;
    tsl::elm::NamedStepTrackBar* m_stickTravelBar = nullptr;
    tsl::elm::StepTrackBar* m_redBar = nullptr;
    tsl::elm::StepTrackBar* m_greenBar = nullptr;
    tsl::elm::StepTrackBar* m_blueBar = nullptr;
};

class HudGui : public tsl::Gui {
public:
    virtual tsl::elm::Element* createUI() override {
        return new HudCanvas();
    }

    virtual void update() override;

    virtual bool handleInput(u64 keysDown, u64, const HidTouchState&, HidAnalogStickState, HidAnalogStickState) override;
};

class GamepadOverlay : public tsl::Overlay {
public:
    GamepadOverlay() {
        s_instance = this;
    }

    static GamepadOverlay& instance() {
        return *s_instance;
    }

    SharedState& state() {
        return m_state;
    }

    void openHud() {
        this->m_hudActive = true;
        this->m_returnToConfigOnShow = false;
        tsl::hlp::requestForeground(false);
        tsl::changeTo<HudGui>();
    }

    void closeHudToConfig() {
        this->m_hudActive = false;
        this->m_returnToConfigOnShow = false;
        tsl::hlp::requestForeground(true);
        tsl::goBack();
    }

    void drawHud(tsl::gfx::Renderer* renderer) {
        renderer->clearScreen();

        const auto& config = this->m_state.config;
        const s32 width = static_cast<s32>(std::lround(kBaseWidth * config.scale));
        const s32 height = static_cast<s32>(std::lround(kBaseHeight * config.scale));
        const s32 margin = static_cast<s32>(std::lround(kMargin * config.scale));

        s32 x = margin;
        s32 y = margin;

        switch (config.corner) {
            case OverlayConfig::Corner::TopLeft:
                break;
            case OverlayConfig::Corner::BottomLeft:
                y = tsl::cfg::FramebufferHeight - height - margin;
                break;
        }

        if (config.backgroundPanel) {
            const s32 padding = static_cast<s32>(std::lround(10.0F * config.scale));
            drawBackdropPanel(
                renderer,
                x - padding,
                y - padding,
                width + padding * 2,
                height + padding * 2,
                { 0x0, 0x0, 0x0, kBackdropAlpha }
            );
        }

        drawGamepad(renderer, this->m_state.preview, config, x, y, config.scale);
    }

    virtual void initServices() override {
        this->m_state.init();
    }

    virtual void exitServices() override {}

    virtual void onShow() override {
        if (this->m_returnToConfigOnShow) {
            this->m_returnToConfigOnShow = false;
            this->m_hudActive = false;
            tsl::goBack();
        }
    }

    virtual void onHide() override {
        if (this->m_hudActive) {
            this->m_returnToConfigOnShow = true;
            tsl::hlp::requestForeground(true);
        }
    }

    virtual std::unique_ptr<tsl::Gui> loadInitialGui() override {
        return initially<ConfigGui>();
    }

private:
    inline static GamepadOverlay* s_instance = nullptr;

    SharedState m_state;
    bool m_hudActive = false;
    bool m_returnToConfigOnShow = false;
};

void HudCanvas::draw(tsl::gfx::Renderer* renderer) {
    GamepadOverlay::instance().drawHud(renderer);
}

tsl::elm::Element* ConfigGui::createUI() {
    auto* frame = new tsl::elm::HeaderOverlayFrame(250);
    frame->setHeader(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer* renderer, s32 x, s32 y, s32 w, s32 h) {
        auto& shared = GamepadOverlay::instance().state();

        renderer->drawString("Gamepad Overlay", false, x + 20, y + 38, 30, tsl::gfx::Renderer::a(tsl::style::color::ColorText));
        renderer->drawString("config", false, x + 20, y + 60, 15, tsl::gfx::Renderer::a(tsl::style::color::ColorDescription));

        const float fitScale = std::min(
            shared.config.scale,
            std::min((static_cast<float>(w) - 40.0F) / kBaseWidth, (static_cast<float>(h) - 85.0F) / kBaseHeight)
        );
        const float previewScale = std::clamp(fitScale, 0.50F, 1.50F);
        const s32 previewWidth = static_cast<s32>(std::lround(kBaseWidth * previewScale));
        const s32 previewHeight = static_cast<s32>(std::lround(kBaseHeight * previewScale));
        const s32 previewX = x + (w - previewWidth) / 2;
        const s32 previewY = y + 85 + std::max(0, (h - 95 - previewHeight) / 2);

        if (shared.config.backgroundPanel) {
            drawBackdropPanel(
                renderer,
                previewX - 8,
                previewY - 8,
                previewWidth + 16,
                previewHeight + 16,
                { 0x0, 0x0, 0x0, kBackdropAlpha }
            );
        }

        drawGamepad(renderer, shared.preview, shared.config, previewX, previewY, previewScale);
    }));

    auto* list = new tsl::elm::List();

    list->addItem(new tsl::elm::CategoryHeader("Launch"));
    auto* hudItem = new tsl::elm::ListItem("Start", "A");
    hudItem->setClickListener([](u64 keys) {
        if (!(keys & HidNpadButton_A))
            return false;

        GamepadOverlay::instance().openHud();
        return true;
    });
    list->addItem(hudItem);

    list->addItem(new tsl::elm::CategoryHeader("Overlay", true));

    m_cornerItem = new tsl::elm::ListItem("Corner", cornerToString(GamepadOverlay::instance().state().config.corner));
    m_cornerItem->setClickListener([this](u64 keys) {
        if (keys & (HidNpadButton_A | HidNpadButton_AnyRight)) {
            this->cycleCorner(1);
            return true;
        }
        if (keys & HidNpadButton_AnyLeft) {
            this->cycleCorner(-1);
            return true;
        }
        return false;
    });
    list->addItem(m_cornerItem);

    auto* scaleItem = new tsl::elm::ListItem("Scale", kScaleLabels[getScaleIndex(GamepadOverlay::instance().state().config.scale)]);
    list->addItem(scaleItem);
    m_scaleBar = new tsl::elm::NamedStepTrackBar("S", { "0.50x", "0.75x", "1.00x", "1.25x", "1.50x", "1.75x", "2.00x" });
    m_scaleBar->setProgress(static_cast<u8>(getScaleIndex(GamepadOverlay::instance().state().config.scale)));
    m_scaleBar->setValueChangedListener([scaleItem](u8 value) {
        auto& shared = GamepadOverlay::instance().state();
        shared.config.scale = kScaleValues[value];
        scaleItem->setValue(kScaleLabels[value]);
        shared.persist();
    });
    list->addItem(m_scaleBar);

    list->addItem(new tsl::elm::CategoryHeader("Thumb sticks", true));

    m_stickScaleItem = new tsl::elm::ListItem("Stick size", kStickScaleLabels[getNearestIndex(GamepadOverlay::instance().state().config.stickScale, kStickScaleValues)]);
    list->addItem(m_stickScaleItem);
    m_stickScaleBar = new tsl::elm::NamedStepTrackBar("S", { "0.75x", "1.00x", "1.25x", "1.50x", "1.75x", "2.00x", "2.25x" });
    m_stickScaleBar->setProgress(static_cast<u8>(getNearestIndex(GamepadOverlay::instance().state().config.stickScale, kStickScaleValues)));
    m_stickScaleBar->setValueChangedListener([this](u8 value) {
        auto& shared = GamepadOverlay::instance().state();
        shared.config.stickScale = kStickScaleValues[value];
        shared.persist();
        this->syncWidgets();
    });
    list->addItem(m_stickScaleBar);

    m_stickTravelItem = new tsl::elm::ListItem("Stick travel", kStickTravelLabels[getNearestIndex(GamepadOverlay::instance().state().config.stickTravel, kStickTravelValues)]);
    list->addItem(m_stickTravelItem);
    m_stickTravelBar = new tsl::elm::NamedStepTrackBar("T", { "0.75x", "1.00x", "1.25x", "1.50x", "1.75x", "2.00x", "2.25x" });
    m_stickTravelBar->setProgress(static_cast<u8>(getNearestIndex(GamepadOverlay::instance().state().config.stickTravel, kStickTravelValues)));
    m_stickTravelBar->setValueChangedListener([this](u8 value) {
        auto& shared = GamepadOverlay::instance().state();
        shared.config.stickTravel = kStickTravelValues[value];
        shared.persist();
        this->syncWidgets();
    });
    list->addItem(m_stickTravelBar);

    m_panelToggle = new tsl::elm::ToggleListItem("Background panel", GamepadOverlay::instance().state().config.backgroundPanel, "On", "Off");
    m_panelToggle->setStateChangedListener([this](bool state) {
        auto& shared = GamepadOverlay::instance().state();
        shared.config.backgroundPanel = state;
        shared.persist();
        this->syncWidgets();
    });
    list->addItem(m_panelToggle);

    list->addItem(new tsl::elm::CategoryHeader("Accent color", true));
    m_colorValueItem = new tsl::elm::ListItem("Pressed color", formatColorHex(GamepadOverlay::instance().state().config.accent));
    list->addItem(m_colorValueItem);

    m_redBar = new tsl::elm::StepTrackBar("R", 16);
    m_redBar->setProgress(GamepadOverlay::instance().state().config.accent.r);
    m_redBar->setValueChangedListener([this](u8 value) {
        auto& shared = GamepadOverlay::instance().state();
        shared.config.accent.r = value;
        shared.persist();
        this->syncWidgets();
    });
    list->addItem(m_redBar);

    m_greenBar = new tsl::elm::StepTrackBar("G", 16);
    m_greenBar->setProgress(GamepadOverlay::instance().state().config.accent.g);
    m_greenBar->setValueChangedListener([this](u8 value) {
        auto& shared = GamepadOverlay::instance().state();
        shared.config.accent.g = value;
        shared.persist();
        this->syncWidgets();
    });
    list->addItem(m_greenBar);

    m_blueBar = new tsl::elm::StepTrackBar("B", 16);
    m_blueBar->setProgress(GamepadOverlay::instance().state().config.accent.b);
    m_blueBar->setValueChangedListener([this](u8 value) {
        auto& shared = GamepadOverlay::instance().state();
        shared.config.accent.b = value;
        shared.persist();
        this->syncWidgets();
    });
    list->addItem(m_blueBar);

    auto* resetItem = new tsl::elm::ListItem("Reset defaults", "A");
    resetItem->setClickListener([this](u64 keys) {
        if (!(keys & HidNpadButton_A))
            return false;

        auto& shared = GamepadOverlay::instance().state();
        shared.resetDefaults();
        this->syncWidgets();
        return true;
    });
    list->addItem(resetItem);

    frame->setContent(list);
    this->syncWidgets();
    return frame;
}

void ConfigGui::update() {
    GamepadOverlay::instance().state().updatePreview();
}

void ConfigGui::syncWidgets() {
    auto& config = GamepadOverlay::instance().state().config;

    if (m_cornerItem != nullptr)
        m_cornerItem->setValue(cornerToString(config.corner));
    if (m_stickScaleItem != nullptr)
        m_stickScaleItem->setValue(kStickScaleLabels[getNearestIndex(config.stickScale, kStickScaleValues)]);
    if (m_stickTravelItem != nullptr)
        m_stickTravelItem->setValue(kStickTravelLabels[getNearestIndex(config.stickTravel, kStickTravelValues)]);
    if (m_colorValueItem != nullptr)
        m_colorValueItem->setValue(formatColorHex(config.accent));
    if (m_panelToggle != nullptr)
        m_panelToggle->setState(config.backgroundPanel);
    if (m_scaleBar != nullptr)
        m_scaleBar->setProgress(static_cast<u8>(getScaleIndex(config.scale)));
    if (m_stickScaleBar != nullptr)
        m_stickScaleBar->setProgress(static_cast<u8>(getNearestIndex(config.stickScale, kStickScaleValues)));
    if (m_stickTravelBar != nullptr)
        m_stickTravelBar->setProgress(static_cast<u8>(getNearestIndex(config.stickTravel, kStickTravelValues)));
    if (m_redBar != nullptr)
        m_redBar->setProgress(config.accent.r);
    if (m_greenBar != nullptr)
        m_greenBar->setProgress(config.accent.g);
    if (m_blueBar != nullptr)
        m_blueBar->setProgress(config.accent.b);
}

void ConfigGui::cycleCorner(int delta) {
    auto& shared = GamepadOverlay::instance().state();
    int index = 0;

    switch (shared.config.corner) {
        case OverlayConfig::Corner::TopLeft:
            index = 0;
            break;
        case OverlayConfig::Corner::BottomLeft:
            index = 1;
            break;
    }

    index = (index + delta + 2) % 2;

    switch (index) {
        case 0:
            shared.config.corner = OverlayConfig::Corner::TopLeft;
            break;
        case 1:
            shared.config.corner = OverlayConfig::Corner::BottomLeft;
            break;
        default:
            shared.config.corner = OverlayConfig::Corner::BottomLeft;
            break;
    }

    shared.persist();
    this->syncWidgets();
}

void HudGui::update() {
    GamepadOverlay::instance().state().updatePreview();
}

bool HudGui::handleInput(u64 keysDown, u64, const HidTouchState&, HidAnalogStickState, HidAnalogStickState) {
    if (keysDown & HidNpadButton_B)
        return true;

    return false;
}

bool ConfigGui::handleInput(u64 keysDown, u64, const HidTouchState&, HidAnalogStickState, HidAnalogStickState) {
    if (keysDown & HidNpadButton_X) {
        tsl::setNextOverlay(TESLA_MENU_PATH);
        GamepadOverlay::instance().close();
        return true;
    }

    return false;
}

} // namespace

int main(int argc, char** argv) {
    return tsl::loop<GamepadOverlay, tsl::impl::LaunchFlags::None>(argc, argv);
}
