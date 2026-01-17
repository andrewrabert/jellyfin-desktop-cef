#pragma once

#include "input_layer.h"
#include "../ui/menu_overlay.h"

// Input layer for context menu - only in stack when menu is open
class MenuLayer : public InputLayer {
public:
    explicit MenuLayer(MenuOverlay* menu) : menu_(menu) {}

    bool handleInput(const SDL_Event& event) override {
        if (!menu_) return false;

        switch (event.type) {
            case SDL_EVENT_MOUSE_MOTION:
                menu_->handleMouseMove(
                    static_cast<int>(event.motion.x),
                    static_cast<int>(event.motion.y));
                return true;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                menu_->handleMouseClick(
                    static_cast<int>(event.button.x),
                    static_cast<int>(event.button.y),
                    true);
                return true;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                menu_->handleMouseClick(
                    static_cast<int>(event.button.x),
                    static_cast<int>(event.button.y),
                    false);
                return true;

            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE) {
                    menu_->close();
                    return true;
                }
                return menu_->handleKeyDown(event.key.key);

            default:
                return true;  // Consume all other input when menu open
        }
    }

private:
    MenuOverlay* menu_ = nullptr;
};
