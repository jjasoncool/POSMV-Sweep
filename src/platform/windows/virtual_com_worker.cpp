#include "platform/virtual_com_worker.hpp"
#include "environment/virtual_com.hpp"
#include "transport/serial_transport.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <setupapi.h>
#include <shellapi.h>
namespace posmv {
namespace {
namespace fs = std::filesystem;
// Pinned com0com build whose catalog chains to a pre-2015 cross-signed certificate; see
// docs/verification/2026-10-01-com0com-2.2.2.0.md for why 3.0.0.0 cannot load under Secure Boot.
constexpr wchar_t package_name[] = L"com0com-2.2.2.0-x64-fre-signed.zip";
constexpr wchar_t package_url[] =
    L"https://downloads.sourceforge.net/project/com0com/com0com/2.2.2.0/com0com-2.2.2.0-x64-fre-signed.zip";
constexpr char package_sha256[] = "3bc7313e8577774ce78cff4d1c816faf96cfa48c485bbcc5527d4569f7d5e373";
constexpr char test_frame[] = ":000011 -0355G-0053  0129\r\n";

struct Report {
    bool ok = true;
    std::vector<std::string> messages;
    std::optional<EnvironmentRoles> roles;
    void say(std::string text) { messages.push_back(std::move(text)); }
};

std::string utf8(std::wstring_view text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}
std::wstring wide(std::string_view text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}
std::string error_text(DWORD code) {
    return std::system_category().message(static_cast<int>(code)) + " (" + std::to_string(code) + ")";
}
fs::path environment_path(const wchar_t* name) {
    std::wstring value(32767, L'\0');
    const DWORD length = GetEnvironmentVariableW(name, value.data(), static_cast<DWORD>(value.size()));
    if (length == 0 || length >= value.size()) return {};
    value.resize(length);
    return value;
}
std::string read_text(const fs::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input) throw std::runtime_error("無法讀取 " + utf8(file.wstring()));
    return {std::istreambuf_iterator<char>(input), {}};
}
void write_text_atomically(const fs::path& file, const std::string& text) {
    const auto temporary = fs::path(file).concat(L".tmp");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << text;
        if (!output.flush()) throw std::runtime_error("無法寫入 " + utf8(temporary.wstring()));
    }
    if (!MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("無法更新 " + utf8(file.wstring()) + "：" + error_text(GetLastError()));
}

// --- Machine state (readable without elevation) -------------------------------------------
class RegistryKey {
public:
    RegistryKey(HKEY root, const wchar_t* path) { if (RegOpenKeyExW(root, path, 0, KEY_READ, &handle_) != ERROR_SUCCESS) handle_ = nullptr; }
    ~RegistryKey() { if (handle_) RegCloseKey(handle_); }
    RegistryKey(const RegistryKey&) = delete;
    RegistryKey& operator=(const RegistryKey&) = delete;
    HKEY get() const { return handle_; }
private:
    HKEY handle_{};
};
std::optional<fs::path> setup_directory() {
    for (const auto* variable : {L"ProgramFiles(x86)", L"ProgramFiles"}) {
        const auto base = environment_path(variable);
        if (!base.empty() && fs::exists(base / L"com0com" / L"setupc.exe")) return base / L"com0com";
    }
    return std::nullopt;
}
std::string driver_version(const fs::path& directory) {
    std::ifstream inf(directory / L"com0com.inf");
    for (std::string line; std::getline(inf, line);)
        if (line.find("DriverVer") != std::string::npos) {
            const auto comma = line.find(',');
            if (comma == std::string::npos) break;
            auto version = line.substr(comma + 1);
            while (!version.empty() && (version.back() == '\r' || version.back() == ' ')) version.pop_back();
            return version;
        }
    return "未知";
}
// Driver store copies (%windir%\INF\oem*.inf) of com0com's own INF: its device class and provider.
std::vector<fs::path> com0com_driver_store_infs() {
    std::vector<fs::path> infs;
    std::wstring windows(MAX_PATH, L'\0');
    windows.resize(GetWindowsDirectoryW(windows.data(), MAX_PATH));
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(fs::path(windows) / L"INF", error)) {
        const auto name = entry.path().filename().wstring();
        if (!name.starts_with(L"oem") || !entry.path().extension().wstring().ends_with(L"inf")) continue;
        std::string text;
        try { text = read_text(entry.path()); } catch (const std::exception&) { continue; }
        std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (text.find("{df799e12-3c56-421b-b298-b6d3642bc878}") != std::string::npos &&
            text.find("vyacheslav frolov") != std::string::npos) infs.push_back(entry.path());
    }
    return infs;
}
bool driver_service_registered() {
    return RegistryKey(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\com0com").get() != nullptr;
}
std::map<std::string, std::string> com0com_port_names() {
    std::map<std::string, std::string> names;
    RegistryKey parameters(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\com0com\\Parameters");
    if (!parameters.get()) return names;
    for (DWORD index = 0;; ++index) {
        wchar_t key[256];
        DWORD key_length = 256;
        if (RegEnumKeyExW(parameters.get(), index, key, &key_length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        wchar_t port[256];
        DWORD size = sizeof(port);
        // com0com uses the endpoint key itself (CNCA0) when no PortName was assigned.
        const bool named = RegGetValueW(parameters.get(), key, L"PortName", RRF_RT_REG_SZ, nullptr, port, &size) == ERROR_SUCCESS;
        names[utf8(key)] = utf8(named ? port : key);
    }
    return names;
}
// Bus devices (ROOT\CNCPORTS for 2.x, ROOT\COM0COM for 3.x) store their pair number as PortNum.
std::vector<unsigned> com0com_bus_ids() {
    std::vector<unsigned> ids;
    for (const auto* enumerator : {L"SYSTEM\\CurrentControlSet\\Enum\\ROOT\\CNCPORTS", L"SYSTEM\\CurrentControlSet\\Enum\\ROOT\\COM0COM"}) {
        RegistryKey buses(HKEY_LOCAL_MACHINE, enumerator);
        if (!buses.get()) continue;
        for (DWORD index = 0;; ++index) {
            wchar_t instance[256];
            DWORD length = 256;
            if (RegEnumKeyExW(buses.get(), index, instance, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
            const auto parameters = std::wstring(instance) + L"\\Device Parameters";
            DWORD id{}, size = sizeof(id);
            if (RegGetValueW(buses.get(), parameters.c_str(), L"PortNum", RRF_RT_REG_DWORD, nullptr, &id, &size) == ERROR_SUCCESS)
                ids.push_back(id);
        }
    }
    return ids;
}
std::vector<std::uint8_t> com_db() {
    const auto* path = L"SYSTEM\\CurrentControlSet\\Control\\COM Name Arbiter";
    DWORD size = 0;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, path, L"ComDB", RRF_RT_REG_BINARY, nullptr, nullptr, &size) != ERROR_SUCCESS) return {};
    std::vector<std::uint8_t> bitmap(size);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, path, L"ComDB", RRF_RT_REG_BINARY, nullptr, bitmap.data(), &size) != ERROR_SUCCESS) return {};
    bitmap.resize(size);
    return bitmap;
}
std::vector<std::string> active_ports() {
    std::vector<std::string> ports;
    RegistryKey serial(HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\SERIALCOMM");
    if (!serial.get()) return ports;
    for (DWORD index = 0;; ++index) {
        wchar_t name[256], data[256];
        DWORD name_length = 256, data_size = sizeof(data), type{};
        const auto status = RegEnumValueW(serial.get(), index, name, &name_length, nullptr, &type,
            reinterpret_cast<BYTE*>(data), &data_size);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status != ERROR_SUCCESS || type != REG_SZ) continue;
        ports.push_back(utf8(std::wstring_view(data, wcsnlen(data, data_size / sizeof(wchar_t)))));
    }
    return ports;
}
VirtualComMachine read_machine() {
    return {setup_directory().has_value(), com0com_pairs(com0com_port_names(), com0com_bus_ids()), com_db(), active_ports()};
}

// --- Ownership (machine-wide; written only by elevated actions) ----------------------------
fs::path state_directory() { return environment_path(L"ProgramData") / L"POSMV-Sweep" / L"virtual-com"; }
fs::path ownership_file() { return state_directory() / L"ownership.txt"; }
VirtualComOwnership load_ownership(const fs::path& legacy) {
    if (fs::exists(ownership_file())) return parse_ownership(read_text(ownership_file()));
    if (!legacy.empty() && fs::exists(legacy)) return import_legacy_ownership(read_text(legacy));
    return {};
}
void save_ownership(const VirtualComOwnership& ownership) {
    fs::create_directories(state_directory());
    write_text_atomically(ownership_file(), format_ownership(ownership));
}
// Elevated actions hold this so two Simulators cannot change pairs at the same time.
class OperationLock {
public:
    OperationLock() {
        fs::create_directories(state_directory());
        handle_ = CreateFileW((state_directory() / L"operation.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) throw std::runtime_error("另一個 Virtual COM 管理操作正在進行，請稍後再試");
    }
    ~OperationLock() { CloseHandle(handle_); }
    OperationLock(const OperationLock&) = delete;
    OperationLock& operator=(const OperationLock&) = delete;
private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

// --- Processes ------------------------------------------------------------------------------
struct ProcessResult { DWORD exit_code{}; std::string output; };
ProcessResult run(const fs::path& program, const std::wstring& arguments, const fs::path& directory,
    std::chrono::milliseconds timeout, bool capture) {
    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    HANDLE read_end{}, write_end{};
    if (capture) {
        if (!CreatePipe(&read_end, &write_end, &inherit, 0)) throw std::runtime_error("CreatePipe：" + error_text(GetLastError()));
        SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW | (capture ? STARTF_USESTDHANDLES : 0);
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = startup.hStdError = write_end;
    std::wstring command = L"\"" + program.wstring() + L"\" " + arguments;
    PROCESS_INFORMATION process{};
    const bool started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, capture, CREATE_NO_WINDOW, nullptr,
        directory.empty() ? nullptr : directory.c_str(), &startup, &process);
    const DWORD start_error = GetLastError();
    if (write_end) CloseHandle(write_end);
    if (!started) {
        if (read_end) CloseHandle(read_end);
        throw std::runtime_error("無法執行 " + utf8(program.filename().wstring()) + "：" + error_text(start_error));
    }
    ProcessResult result;
    std::thread reader;
    if (capture) reader = std::thread([&] {
        std::array<char, 4096> buffer{};
        DWORD count{};
        while (ReadFile(read_end, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr) && count)
            result.output.append(buffer.data(), count);
    });
    const bool finished = WaitForSingleObject(process.hProcess, static_cast<DWORD>(timeout.count())) == WAIT_OBJECT_0;
    if (!finished) TerminateProcess(process.hProcess, 1);
    GetExitCodeProcess(process.hProcess, &result.exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (reader.joinable()) reader.join();
    if (read_end) CloseHandle(read_end);
    if (!finished) throw std::runtime_error(utf8(program.filename().wstring()) + " 逾時未結束，已終止");
    return result;
}
fs::path system_tool(const wchar_t* name) {
    std::wstring directory(MAX_PATH, L'\0');
    directory.resize(GetSystemDirectoryW(directory.data(), MAX_PATH));
    return fs::path(directory) / name;
}
ProcessResult setupc(const fs::path& directory, const std::wstring& arguments) {
    // setupc opens com0com.inf from its working directory.
    auto result = run(directory / L"setupc.exe", arguments, directory, std::chrono::minutes(2), true);
    if (result.exit_code != 0)
        throw std::runtime_error("setupc " + utf8(arguments) + " 失敗（exit " + std::to_string(result.exit_code) + "）：" + result.output);
    return result;
}

// --- Driver package ---------------------------------------------------------------------------
std::string sha256(const fs::path& file) {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) throw std::runtime_error("SHA-256 無法使用");
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("SHA-256 無法使用");
    }
    std::ifstream input(file, std::ios::binary);
    std::array<char, 65536> buffer{};
    while (input.read(buffer.data(), buffer.size()) || input.gcount() > 0)
        BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(input.gcount()), 0);
    std::array<UCHAR, 32> digest{};
    BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    std::string hex;
    for (const auto byte : digest) {
        constexpr char digits[] = "0123456789abcdef";
        hex += digits[byte >> 4];
        hex += digits[byte & 0x0F];
    }
    return hex;
}
fs::path application_directory() {
    std::wstring path(32768, L'\0');
    path.resize(GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size())));
    return fs::path(path).parent_path();
}
// Bundled copy first (portable ZIP), then an earlier download; otherwise download the pinned file.
fs::path driver_package(Report& report) {
    const fs::path candidates[] = {application_directory() / L"drivers" / package_name, state_directory() / package_name};
    const char* origins[] = {"ZIP 內附", "先前下載"};
    for (std::size_t i = 0; i < std::size(candidates); ++i) {
        if (!fs::exists(candidates[i])) continue;
        if (sha256(candidates[i]) == package_sha256) {
            report.say(std::string("安裝檔來源：") + origins[i] + "（SHA-256 相符）");
            return candidates[i];
        }
        report.say(std::string(origins[i]) + "的安裝檔 SHA-256 不符，已略過：" + utf8(candidates[i].wstring()));
    }
    const auto download = state_directory() / (std::wstring(package_name) + L".download");
    fs::remove(download);
    const auto result = run(system_tool(L"curl.exe"), L"-fsSL --max-time 120 -o \"" + download.wstring() + L"\" " + package_url,
        {}, std::chrono::minutes(3), true);
    if (result.exit_code != 0) throw std::runtime_error("下載 com0com 失敗（curl exit " + std::to_string(result.exit_code) + "）：" + result.output);
    if (sha256(download) != package_sha256) {
        fs::remove(download);
        throw std::runtime_error("下載的 com0com 安裝檔 SHA-256 不符，已刪除，沒有執行");
    }
    const auto cached = state_directory() / package_name;
    fs::remove(cached);  // a mismatching earlier copy was skipped above
    fs::rename(download, cached);
    report.say("安裝檔來源：網路下載（SHA-256 相符）");
    return cached;
}

// --- Actions --------------------------------------------------------------------------------
std::string pair_text(const VirtualPair& pair) { return pair.a + "↔" + pair.b; }
bool contains(const std::vector<std::string>& ports, const std::string& port) {
    return std::ranges::find(ports, port) != ports.end();
}
void describe(Report& report, const VirtualComMachine& machine, const VirtualComOwnership& ownership) {
    if (const auto directory = setup_directory())
        report.say("com0com 驅動：已安裝，版本 " + driver_version(*directory) +
            (ownership.driver_installed_by_us ? "，由本程式安裝" : "，不是由本程式安裝"));
    else
        report.say("com0com 驅動：尚未安裝。請按「安裝 com0com 驅動」");
    for (const auto& owned : ownership.pairs) {
        const auto found = std::ranges::find_if(machine.pairs, [&](const VirtualPair& p) { return p.id == owned.id; });
        if (found == machine.pairs.end()) report.say("本程式配對 " + pair_text(owned) + "：已不存在");
        else if (found->a != owned.a || found->b != owned.b)
            report.say("本程式配對 " + pair_text(owned) + "：埠名稱已被改成 " + pair_text(*found) + "，不做任何變更");
        else if (!contains(machine.active_ports, owned.a) || !contains(machine.active_ports, owned.b))
            report.say("本程式配對 " + pair_text(owned) + "：已登記，但 COM 埠沒有出現（驅動可能被 Windows 擋下，裝置管理員代碼 52）");
        else report.say("本程式配對 " + pair_text(owned) + "：可用");
    }
    for (const auto& pair : machine.pairs) {
        const bool owned = std::ranges::any_of(ownership.pairs, [&](const VirtualPair& p) { return p.id == pair.id; });
        if (!owned) report.say("其他程式的配對 " + pair_text(pair) + "：本程式不會變更");
    }
    report.roles = environment_roles(machine, ownership);
    if (!report.roles && machine.driver_installed) report.say("測試環境尚未就緒：請按「建立兩組測試配對」");
}
void inspect(Report& report, const fs::path& legacy) {
    describe(report, read_machine(), load_ownership(legacy));
}
void install(Report& report, const fs::path& legacy) {
    OperationLock lock;
    auto ownership = load_ownership(legacy);
    if (const auto directory = setup_directory()) {
        report.say("com0com 已安裝（版本 " + driver_version(*directory) + "），不需重新安裝");
        describe(report, read_machine(), ownership);
        return;
    }
    const auto package = driver_package(report);
    const auto unpacked = state_directory() / L"installer";
    fs::remove_all(unpacked);
    fs::create_directories(unpacked);
    const auto tar = run(system_tool(L"tar.exe"), L"-xf \"" + package.wstring() + L"\" -C \"" + unpacked.wstring() + L"\"",
        {}, std::chrono::minutes(1), true);
    if (tar.exit_code != 0) throw std::runtime_error("解壓縮 com0com 安裝檔失敗：" + tar.output);
    const auto installer = unpacked / L"com0com-2.2.2.0-x64-fre-signed" / L"setup.exe";
    // NSIS silent mode installs the driver files only; Create owns pair lifecycles.
    const auto result = run(installer, L"/S", unpacked, std::chrono::minutes(3), false);
    if (result.exit_code != 0) throw std::runtime_error("com0com 安裝程式失敗（exit " + std::to_string(result.exit_code) + "）");
    if (!setup_directory()) throw std::runtime_error("com0com 安裝程式結束，但找不到 setupc.exe");
    ownership.driver_installed_by_us = true;
    save_ownership(ownership);
    fs::remove_all(unpacked);
    report.say("已安裝 com0com 2.2.2.0");
    describe(report, read_machine(), ownership);
}
void create(Report& report, const fs::path& legacy) {
    OperationLock lock;
    const auto directory = setup_directory();
    auto machine = read_machine();
    auto ownership = load_ownership(legacy);
    const auto requests = plan_create(machine, ownership);
    // Records of Owned Pairs removed elsewhere are dropped before adding new ones.
    const auto still_present = plan_remove(machine, ownership);
    std::erase_if(ownership.pairs, [&](const VirtualPair& p) { return std::ranges::find(still_present, p.id) == still_present.end(); });
    save_ownership(ownership);
    for (const auto& request : requests) {
        // Baud-rate and overrun emulation make the link behave like a physical serial line.
        setupc(*directory, L"install PortName=" + wide(request.a) + L",EmuBR=yes,EmuOverrun=yes PortName=" +
            wide(request.b) + L",EmuBR=yes,EmuOverrun=yes");
        machine = read_machine();
        const auto created = std::ranges::find_if(machine.pairs, [&](const VirtualPair& p) { return p.a == request.a && p.b == request.b; });
        if (created == machine.pairs.end()) throw std::runtime_error("setupc 回報成功，但找不到新配對 " + request.a + "↔" + request.b);
        // Record ownership before checking the ports, so a blocked driver can still be cleaned up.
        ownership.pairs.push_back(*created);
        save_ownership(ownership);
        report.say("已建立配對 " + pair_text(*created));
    }
    for (int attempt = 0; attempt < 10 && !environment_roles(machine, ownership); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        machine = read_machine();
    }
    describe(report, machine, ownership);
    if (!report.roles) report.ok = false;
}
void remove(Report& report, const fs::path& legacy) {
    OperationLock lock;
    const auto directory = setup_directory();
    if (!directory) throw std::runtime_error("com0com 沒有安裝，沒有可移除的配對");
    const auto machine = read_machine();
    auto ownership = load_ownership(legacy);
    const auto ids = plan_remove(machine, ownership);
    for (const auto id : ids) {
        setupc(*directory, L"remove " + std::to_wstring(id));
        report.say("已移除本程式配對 " + std::to_string(id));
    }
    if (ids.empty()) report.say("沒有需要移除的本程式配對");
    ownership.pairs.clear();
    save_ownership(ownership);
    describe(report, read_machine(), ownership);
}
void uninstall(Report& report, const fs::path& legacy) {
    OperationLock lock;
    const auto directory = setup_directory();
    if (!directory) {
        report.say("com0com 沒有安裝，不需解除安裝");
        return;
    }
    auto ownership = load_ownership(legacy);
    const auto plan = plan_uninstall(read_machine(), ownership);
    if (!plan.allowed) {
        report.ok = false;
        report.say(plan.reason);
        return;
    }
    for (const auto id : plan.remove_first) setupc(*directory, L"remove " + std::to_wstring(id));
    // The 2.2.2.0 uninstaller deletes its oem*.inf copy directly and orphans the driver store
    // package, so release the package first, while Windows can still find it (fails if in use).
    for (const auto& inf : com0com_driver_store_infs()) {
        const auto name = utf8(inf.filename().wstring());
        if (SetupUninstallOEMInfW(inf.filename().c_str(), 0, nullptr)) report.say("已從 Windows 驅動程式庫移除 " + name);
        else report.say("無法從 Windows 驅動程式庫移除 " + name + "：" + error_text(GetLastError()));
    }
    // Run the NSIS uninstaller in place (_?=) so we can wait for it.
    const auto result = run(*directory / L"uninstall.exe", L"/S _?=" + directory->wstring(), *directory, std::chrono::minutes(3), false);
    if (result.exit_code != 0) throw std::runtime_error("com0com 解除安裝程式失敗（exit " + std::to_string(result.exit_code) + "）");
    std::error_code ignored;
    fs::remove_all(*directory, ignored);
    ownership = {};
    save_ownership(ownership);
    report.say(driver_service_registered()
        ? "已移除 com0com 程式檔，但驅動服務仍在：請重新開機完成移除"
        : "已解除安裝 com0com");
}
double exchange(const std::string& from, const std::string& to) {
    SerialTransport sender(from, 38400), receiver(to, 38400);
    std::array<char, 256> buffer{};
    while (receiver.read(buffer) > 0) {}
    const auto start = std::chrono::steady_clock::now();
    sender.write(test_frame);
    std::string received;
    while (received.size() < sizeof(test_frame) - 1 && std::chrono::steady_clock::now() - start < std::chrono::seconds(2))
        received.append(buffer.data(), receiver.read(buffer));
    if (received != test_frame)
        throw std::runtime_error(from + "→" + to + "：收到的資料不符（" + std::to_string(received.size()) + " bytes）");
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
void self_test(Report& report, const fs::path& legacy) {
    const auto machine = read_machine();
    const auto ownership = load_ownership(legacy);
    const auto roles = environment_roles(machine, ownership);
    if (!roles) {
        describe(report, machine, ownership);
        throw std::runtime_error("測試環境尚未就緒，無法自測");
    }
    const std::pair<std::string, std::string> links[] = {
        {roles->tx, roles->input}, {roles->input, roles->tx}, {roles->output, roles->rx}, {roles->rx, roles->output}};
    for (const auto& [from, to] : links) {
        std::ostringstream line;
        line.precision(1);
        line << std::fixed << from << "→" << to << "：通過（" << exchange(from, to) << " ms）";
        report.say(line.str());
    }
    report.say("雙向自測通過（38400 bps，8N1）");
    report.roles = roles;
}
void write_report(const fs::path& file, const Report& report) {
    std::string text = std::string("status=") + (report.ok ? "ok" : "error") + "\n";
    for (const auto& message : report.messages) text += "message=" + message + "\n";
    if (report.roles)
        text += "roles=" + report.roles->tx + " " + report.roles->input + " " + report.roles->output + " " + report.roles->rx + "\n";
    fs::create_directories(file.parent_path());
    write_text_atomically(file, text);
}
}
int run_virtual_com_worker() {
    int count = 0;
    auto** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return 2;
    std::vector<std::wstring> args(arguments, arguments + count);
    LocalFree(arguments);
    // args: <exe> env <action> --result FILE [--legacy FILE]
    std::wstring action;
    fs::path result, legacy;
    for (std::size_t i = 2; i < args.size(); ++i) {
        if (args[i] == L"--result" && i + 1 < args.size()) result = args[++i];
        else if (args[i] == L"--legacy" && i + 1 < args.size()) legacy = args[++i];
        else if (action.empty()) action = args[i];
    }
    if (result.empty()) return 2;
    Report report;
    try {
        if (action == L"inspect") inspect(report, legacy);
        else if (action == L"install") install(report, legacy);
        else if (action == L"create") create(report, legacy);
        else if (action == L"remove") remove(report, legacy);
        else if (action == L"uninstall") uninstall(report, legacy);
        else if (action == L"test") self_test(report, legacy);
        else throw std::invalid_argument("未知的環境動作：" + utf8(action));
    } catch (const std::exception& e) {
        report.ok = false;
        report.say(e.what());
    }
    try { write_report(result, report); } catch (...) { return 3; }
    return report.ok ? 0 : 1;
}
}
