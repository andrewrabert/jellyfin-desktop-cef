#include "cef_client.h"
#include <iostream>

Client::Client(int width, int height, PaintCallback on_paint)
    : width_(width), height_(height), on_paint_(std::move(on_paint)) {}

void Client::GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) {
    rect.Set(0, 0, width_, height_);
}

void Client::OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type,
                     const RectList& dirtyRects, const void* buffer,
                     int width, int height) {
    if (on_paint_) {
        on_paint_(buffer, width, height);
    }
}

void Client::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    std::cout << "Browser created" << std::endl;
}

void Client::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
    std::cout << "Browser closing" << std::endl;
    is_closed_ = true;
}
