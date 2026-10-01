#pragma once
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
namespace posmv {
// Non-blocking native process boundary; no GTK or OS handles in public API.
class EnvironmentManager {
public:
    EnvironmentManager();
    ~EnvironmentManager();
    EnvironmentManager(const EnvironmentManager&) = delete;
    EnvironmentManager& operator=(const EnvironmentManager&) = delete;
    bool active() const;
    void start(const std::string& action, const std::filesystem::path& state_directory);
    std::optional<unsigned long> poll();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}