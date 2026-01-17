#pragma once

#include <vector>
#include <algorithm>

// Interface for window state change notifications
class WindowStateListener {
public:
    virtual ~WindowStateListener() = default;
    virtual void onMinimized() {}
    virtual void onRestored() {}
    virtual void onFocusGained() {}
    virtual void onFocusLost() {}
};

// Broadcasts window state changes to all listeners
class WindowStateNotifier {
public:
    void add(WindowStateListener* listener) {
        listeners_.push_back(listener);
    }

    void remove(WindowStateListener* listener) {
        listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), listener), listeners_.end());
    }

    void notifyMinimized() {
        for (auto* l : listeners_) l->onMinimized();
    }

    void notifyRestored() {
        for (auto* l : listeners_) l->onRestored();
    }

    void notifyFocusGained() {
        for (auto* l : listeners_) l->onFocusGained();
    }

    void notifyFocusLost() {
        for (auto* l : listeners_) l->onFocusLost();
    }

private:
    std::vector<WindowStateListener*> listeners_;
};
