#pragma once

#include <string>
#include <mutex>

class Settings {
public:
    static Settings& instance();

    bool load();
    bool save();
    void saveAsync();  // Fire-and-forget async save

    const std::string& serverUrl() const { return server_url_; }
    void setServerUrl(const std::string& url) { server_url_ = url; }

private:
    Settings() = default;
    std::string getConfigPath();

    std::string server_url_;
    std::mutex save_mutex_;  // Prevent concurrent saves
};
