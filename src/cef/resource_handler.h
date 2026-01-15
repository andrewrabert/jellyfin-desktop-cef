#pragma once

#include "include/cef_scheme.h"
#include "include/cef_resource_handler.h"
#include "embedded_resources.h"

class EmbeddedSchemeHandlerFactory : public CefSchemeHandlerFactory {
public:
    CefRefPtr<CefResourceHandler> Create(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        const CefString& scheme_name,
        CefRefPtr<CefRequest> request) override;

    IMPLEMENT_REFCOUNTING(EmbeddedSchemeHandlerFactory);
};

class EmbeddedResourceHandler : public CefResourceHandler {
public:
    EmbeddedResourceHandler(const EmbeddedResource& resource);

    bool Open(CefRefPtr<CefRequest> request,
              bool& handle_request,
              CefRefPtr<CefCallback> callback) override;

    void GetResponseHeaders(CefRefPtr<CefResponse> response,
                           int64_t& response_length,
                           CefString& redirect_url) override;

    bool Read(void* data_out,
              int bytes_to_read,
              int& bytes_read,
              CefRefPtr<CefResourceReadCallback> callback) override;

    void Cancel() override {}

private:
    const EmbeddedResource& resource_;
    size_t offset_ = 0;

    IMPLEMENT_REFCOUNTING(EmbeddedResourceHandler);
};
