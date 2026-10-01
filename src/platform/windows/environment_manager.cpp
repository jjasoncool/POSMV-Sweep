#include "platform/environment_manager.hpp"
#include <windows.h>
#include <shellapi.h>
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
    if (action != "Inspect" && action != "Install" && action != "Create" && action != "Remove" && action != "Test")
        throw std::invalid_argument("Unknown environment action");
    wchar_t exe[32768];
    const auto length = GetModuleFileNameW(nullptr, exe, 32768);
    if (!length || length >= 32768) throw std::runtime_error("Cannot locate application directory");
    auto script = std::filesystem::path(exe).parent_path() / "Manage-VirtualCom.ps1";
    if (!std::filesystem::exists(script)) throw std::runtime_error("Missing Manage-VirtualCom.ps1");
    const std::wstring verb(action.begin(), action.end());
    const std::wstring args = L"-NoProfile -File \"" + script.wstring() + L"\" -Action " + verb +
        L" -StateDirectory \"" + directory.wstring() + L"\"";
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info); info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = action == "Inspect" || action == "Test" ? L"open" : L"runas";
    info.lpFile = L"powershell.exe"; info.lpParameters = args.c_str(); info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info))
        throw std::system_error(GetLastError(), std::system_category(), "Environment operation");
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