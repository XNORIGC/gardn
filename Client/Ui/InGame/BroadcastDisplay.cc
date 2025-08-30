// Client/Ui/InGame/BroadcastDisplay.cc
#include <Client/Game.hh>
#include <Client/Ui/Extern.hh>
#include <Client/Render/Renderer.hh>
#include <Client/Ui/Ui.hh>

#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <cstdio>

namespace Ui {

    // 参数
    static constexpr double SHOW_DURATION = 8.0;    // 显示时间（秒）
    static constexpr double FADE_DURATION = 1.0;    // 淡出时间（秒）
    static constexpr float  ROW_SPACING = 12.0f;  // 气泡之间的最小间距（垂直）
    static constexpr float  PADDING_X = 18.0f;  // 气泡左右内边距
    static constexpr float  PADDING_Y = 8.0f;   // 气泡上下内边距
    static constexpr float  TEXT_SIZE = 24.0f;  // 文本大小（像素）
    static constexpr float  RADIUS = 12.0f;  // 圆角半径
    static constexpr float  MIN_BOX_WIDTH = 140.0f; // 气泡最小宽度
    static constexpr float  BASE_Y = 60.0f;  // 顶部第一条 y
    static constexpr float  LERP_SPEED = 0.18f;  // y_pos 插值速度
    static constexpr size_t MAX_ACTIVE = 6;      // 同时最多显示条数

    // 默认颜色
    static constexpr uint32_t DEFAULT_BUBBLE_RGBA = 0x80000000; // 半透明黑 (0x80 alpha)
    static constexpr uint32_t DEFAULT_TEXT_RGBA = 0xffffffff; // 不透明白

    struct ActiveMsg {
        std::string text;
        uint32_t bubble_color = DEFAULT_BUBBLE_RGBA; // 含 alpha
        uint32_t text_color = DEFAULT_TEXT_RGBA;   // 含 alpha (0xff)
        double elapsed = 0.0;    // 秒
        float y_pos = BASE_Y;    // 当前绘制 y（动画）
        float target_y = BASE_Y; // 目标 y（根据 index 计算）
        float alpha = 0.0f;      // 当前 alpha（动画用）
    };

    static std::vector<ActiveMsg> active; // newest at index 0

    // 解析 "#RRGGBB" -> rgb24，返回 true/false
    static bool try_parse_rgb_hex(const std::string& s, uint32_t& out_rgb24) {
        if (s.size() != 7 || s[0] != '#') return false;
        unsigned int val = 0;
        if (sscanf(s.c_str() + 1, "%x", &val) != 1) return false;
        out_rgb24 = val & 0x00ffffffu;
        return true;
    }

    // 解析 raw_text，支持前置 0/1/2 个颜色参数
    static void push_parsed_broadcast(const std::string& raw_text) {
        std::istringstream iss(raw_text);
        std::string token1, token2;
        std::string rest;

        ActiveMsg msg;
        msg.bubble_color = DEFAULT_BUBBLE_RGBA;
        msg.text_color = DEFAULT_TEXT_RGBA;
        msg.elapsed = 0.0;
        msg.alpha = 0.0f;

        // 读第一个 token
        if (!(iss >> token1)) return; // 空直接返回

        uint32_t rgb24;
        bool first_is_color = false;
        bool second_is_color = false;

        if (try_parse_rgb_hex(token1, rgb24)) {
            // 第一个是颜色 -> 气泡色（强制半透明）
            msg.bubble_color = (0x80u << 24) | rgb24;
            first_is_color = true;
            if (iss >> token2) {
                if (try_parse_rgb_hex(token2, rgb24)) {
                    // 第二个是颜色 -> 文本色（不透明）
                    msg.text_color = (0xffu << 24) | rgb24;
                    second_is_color = true;
                }
                else {
                    // 第二个不是颜色，作为文本开头
                    rest = token2;
                }
            }
        }
        else {
            // 第一个不是颜色，作为文本开头
            rest = token1;
        }

        // 取余下整行作为剩余文本
        std::string remaining;
        std::getline(iss, remaining);
        if (!remaining.empty()) {
            size_t p = remaining.find_first_not_of(' ');
            if (p != std::string::npos) remaining = remaining.substr(p);
            else remaining.clear();
        }

        std::string final_text;
        if (!rest.empty()) {
            final_text = rest;
            if (!remaining.empty()) {
                final_text += " ";
                final_text += remaining;
            }
        }
        else {
            final_text = remaining;
        }

        // 如果最终仍为空，则回退使用 token1/token2（防止只有颜色参数时为空）
        if (final_text.empty()) {
            if (!first_is_color) final_text = token1;
            else if (!second_is_color && !token2.empty()) final_text = token2;
        }

        // trim
        auto trim = [](std::string& s) {
            size_t a = s.find_first_not_of(' ');
            size_t b = s.find_last_not_of(' ');
            if (a == std::string::npos) { s.clear(); return; }
            s = s.substr(a, b - a + 1);
            };
        trim(final_text);
        if (final_text.empty()) return;

        msg.text = final_text;

        // 插入到开头（最新最上）
        active.insert(active.begin(), msg);

        // 限制最多 MAX_ACTIVE：若超出，移除最后（最旧）
        if (active.size() > MAX_ACTIVE) {
            active.pop_back();
        }

        // 初始化新插入项的 y_pos 以获得更好的弹入感：
        // 新项在插入时 index = 0 -> target_y 为 BASE_Y；为了弹入，从稍上方开始
        if (!active.empty()) {
            active.front().y_pos = BASE_Y - 18.0f; // 从上方轻微下落
        }
    }

    // 每帧推进：消费 Game::broadcasts，更新时间并计算目标位置，插值 y_pos & alpha
    static void step_broadcasts(double dt_ms) {
        double dt = dt_ms / 1000.0;

        // 1) 消费 Game::broadcasts（服务器/其他处推来的 raw 文本）
        while (!Game::broadcasts.empty()) {
            push_parsed_broadcast(Game::broadcasts.front().text);
            Game::broadcasts.erase(Game::broadcasts.begin());
        }

        // 2) 更新时间并删除完全超时的消息
        for (auto it = active.begin(); it != active.end();) {
            it->elapsed += dt;
            if (it->elapsed > (SHOW_DURATION + FADE_DURATION)) {
                it = active.erase(it);
            }
            else ++it;
        }

        // 3) 计算目标 y（index 0 在顶部 BASE_Y，index 增加向下）
        float y = BASE_Y;
        for (size_t i = 0; i < active.size(); ++i) {
            active[i].target_y = y;
            // 这里 boxH 以 TEXT_SIZE + padding 估算（真正高度在绘制时测量一致）
            float boxH = TEXT_SIZE + PADDING_Y * 2.0f;
            y += boxH + ROW_SPACING;
        }

        // 4) 插值 y_pos 与 alpha（淡入/淡出）
        for (auto& m : active) {
            // 插值 y_pos -> 平滑移动
            m.y_pos += (m.target_y - m.y_pos) * LERP_SPEED;

            // alpha：淡入 0.25s，显示，淡出阶段线性递减
            if (m.elapsed < 0.25) {
                m.alpha = float(m.elapsed / 0.25);
            }
            else if (m.elapsed > SHOW_DURATION) {
                double t = (m.elapsed - SHOW_DURATION) / FADE_DURATION;
                if (t > 1.0) t = 1.0;
                m.alpha = float(1.0 - t);
            }
            else {
                m.alpha = 1.0f;
            }
        }
    }

    // 绘制函数：在这里测量文本宽度（确保使用相同文本大小）
    static void draw_broadcasts(Renderer& ctx) {
        ctx.set_text_size(TEXT_SIZE);
        ctx.set_line_width(TEXT_SIZE * 0.12f);
        ctx.center_text_align();
        ctx.center_text_baseline();

        for (auto const& m : active) {
            // 确保测量与绘制使用一致字体尺寸
            ctx.set_text_size(TEXT_SIZE);
            float textWidth = ctx.get_text_size(m.text.c_str());
            float boxW = std::max(MIN_BOX_WIDTH, textWidth + PADDING_X * 2.0f);
            float boxH = TEXT_SIZE + PADDING_Y * 2.0f;

            // 移动到屏幕中点和消息的 y_pos
            ctx.reset_transform();
            ctx.translate(ctx.width * 0.5f, m.y_pos);

            // 背景
            ctx.set_global_alpha(m.alpha);
            ctx.set_fill(m.bubble_color);
            ctx.begin_path();
            ctx.round_rect(-boxW / 2.0f, -boxH / 2.0f, boxW, boxH, RADIUS);
            ctx.fill();

            // 文本
            ctx.set_global_alpha(m.alpha);
            ctx.set_text_size(TEXT_SIZE);
            ctx.set_line_width(TEXT_SIZE * 0.12f);
            ctx.set_fill(m.text_color);
            ctx.fill_text(m.text.c_str());
        }

        ctx.reset_transform();
        ctx.set_global_alpha(1.0f);
    }

    // Element 构造器：添加到 game_ui_window
    Element* make_broadcast_display() {
        class BroadcastElement : public Element {
        public:
            BroadcastElement() : Element(0, 0) {
                style.h_justify = Style::Center;
                style.v_justify = Style::Top;
                style.animate = [](Element*, Renderer&) {
                    step_broadcasts(Ui::dt);
                    };
                // 始终渲染（你可以按需加 should_render）
            }

            virtual void on_render(Renderer& ctx) override {
                draw_broadcasts(ctx);
            }
        };

        return new BroadcastElement();
    }

} // namespace Ui
