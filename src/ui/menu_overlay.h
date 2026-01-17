#pragma once

#include "include/cef_context_menu_handler.h"
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

struct MenuItem {
    int command_id;
    std::string label;
    bool enabled;
};

class MenuOverlay {
public:
    using StateCallback = std::function<void()>;

    MenuOverlay();
    ~MenuOverlay();

    bool init();

    // Callbacks for open/close events
    void setOnOpen(StateCallback cb) { on_open_ = std::move(cb); }
    void setOnClose(StateCallback cb) { on_close_ = std::move(cb); }

    // Open menu with items at position
    void open(int x, int y, const std::vector<MenuItem>& items,
              CefRefPtr<CefRunContextMenuCallback> callback);

    // Close menu (cancel or select)
    void close();
    void select(int index);

    // Input handling - returns true if menu consumed the event
    bool handleMouseMove(int x, int y);
    bool handleMouseClick(int x, int y, bool down);
    bool handleKeyDown(int key);  // ESC to cancel

    bool isOpen() const { return is_open_; }
    bool needsRedraw() const { return needs_redraw_; }
    void clearRedraw() { needs_redraw_ = false; }

    // Blend menu onto BGRA frame buffer (CEF format)
    void blendOnto(uint8_t* frame, int frame_width, int frame_height);

private:
    void render();
    int itemAtPoint(int x, int y) const;

    StateCallback on_open_;
    StateCallback on_close_;
    bool is_open_ = false;
    bool ignore_next_up_ = false;  // Ignore the button-up that opened the menu
    bool needs_redraw_ = false;    // Force compositor update after close
    int menu_x_ = 0;
    int menu_y_ = 0;
    int tex_width_ = 0;
    int tex_height_ = 0;
    int hover_index_ = -1;

    std::vector<MenuItem> items_;
    CefRefPtr<CefRunContextMenuCallback> callback_;
    std::vector<uint8_t> pixels_;

    // Font data
    std::vector<uint8_t> font_data_;
    void* font_info_ = nullptr;  // stbtt_fontinfo*
    float font_scale_ = 0;
    int font_ascent_ = 0;
    int font_descent_ = 0;
    int font_line_height_ = 0;

    static constexpr int FONT_SIZE = 14;
    static constexpr int PADDING_X = 12;
    static constexpr int PADDING_Y = 6;
    static constexpr int ITEM_HEIGHT = 24;
    static constexpr int MIN_WIDTH = 120;
};
