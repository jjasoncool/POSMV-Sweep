#include "platform/environment_manager.hpp"
#include <windows.h>
#include <shellapi.h>
#include <array>
#include <algorithm>
#include <stdexcept>
#include <system_error>
namespace posmv {
struct EnvironmentManager::Impl {
    HANDLE process{};
    ~Impl() { if (process) CloseHandle(process); }
};
EnvironmentManager::EnvironmentManager() : impl_(std::make_unique<Impl>()) {}
EnvironmentManager::~EnvironmentManager() = default;
bool EnvironmentManager::active() const { return impl_->process != nullptr; }
void EnvironmentManager::start(const std::string& action, const std::filesystem::path& directory) {
    if (active()) throw std::runtime_error("Environment operation already running");
    constexpr std::array<const char*, 4> elevated{"install", "create", "remove", "uninstall"};
    const bool needs_elevation = std::ranges::find(elevated, action) != elevated.end();
    if (!needs_elevation && action != "inspect" && action != "test") throw std::invalid_argument("Unknown environment action");
    wchar_t exe[32768];
    const auto length = GetModuleFileNameW(nullptr, exe, 32768);
    if (!length || length >= 32768) throw std::runtime_error("Cannot locate application executable");
    std::filesystem::create_directories(directory);
    const auto result = directory / "result.txt";
    std::filesystem::remove(result);
    // The Simulator runs its own environment worker, through UAC when pairs or the driver change.
    const std::wstring args = L"env " + std::wstring(action.begin(), action.end()) + L" --result \"" + result.wstring() +
        L"\" --legacy \"" + (directory / "owned.json").wstring() + L"\"";
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info); info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = needs_elevation ? L"runas" : L"open";
    info.lpFile = exe; info.lpParameters = args.c_str(); info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info)) {
        const auto error = GetLastError();
        if (error == ERROR_CANCELLED) throw std::runtime_error("已取消 Windows 權限提示，沒有做任何變更");
        throw std::system_error(static_cast<int>(error), std::system_category(), "Environment operation");
    }
    impl_->process = info.hProcess;
}
std::optional<unsigned long> EnvironmentManager::poll() {
    if (!active()) return std::nullopt;
    const auto result = WaitForSingleObject(impl_->process, 0);
    if (result == WAIT_TIMEOUT) return std::nullopt;
    DWORD code = 1;
    if (result == WAIT_OBJECT_0) GetExitCodeProcess(impl_->process, &code);
    CloseHandle(impl_->process); impl_->process = nullptr;
    return code;
}
}
