#pragma once

#include "include/cef_client.h"
#include "include/cef_render_handler.h"
#include <atomic>
#include <functional>

class Client : public CefClient, public CefRenderHandler, public CefLifeSpanHandler {
public:
    using PaintCallback = std::function<void(const void* buffer, int width, int height)>;

    Client(int width, int height, PaintCallback on_paint);

    // CefClient
    CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }

    // CefRenderHandler
    void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;
    void OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type,
                 const RectList& dirtyRects, const void* buffer,
                 int width, int height) override;

    // CefLifeSpanHandler
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

    bool isClosed() const { return is_closed_; }

private:
    int width_;
    int height_;
    PaintCallback on_paint_;
    std::atomic<bool> is_closed_ = false;

    IMPLEMENT_REFCOUNTING(Client);
    DISALLOW_COPY_AND_ASSIGN(Client);
};
