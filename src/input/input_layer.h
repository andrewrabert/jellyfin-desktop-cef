#pragma once

#include <SDL3/SDL_events.h>
#include <vector>
#include <algorithm>

// Base interface for input handling layers
class InputLayer {
public:
    virtual ~InputLayer() = default;

    // Handle an input event. Return true if consumed, false to pass through.
    virtual bool handleInput(const SDL_Event& event) = 0;
};

// Stack-based input routing
class InputStack {
public:
    void push(InputLayer* layer) {
        layers_.push_back(layer);
    }

    void remove(InputLayer* layer) {
        layers_.erase(std::remove(layers_.begin(), layers_.end(), layer), layers_.end());
    }

    // Route event through layers top-to-bottom. First consumer wins.
    bool route(const SDL_Event& event) {
        for (auto it = layers_.rbegin(); it != layers_.rend(); ++it) {
            if ((*it)->handleInput(event)) {
                return true;
            }
        }
        return false;
    }

private:
    std::vector<InputLayer*> layers_;
};
