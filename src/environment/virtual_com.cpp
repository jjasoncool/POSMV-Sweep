#include "environment/virtual_com.hpp"
#include <algorithm>
#include <charconv>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
namespace posmv {
namespace {
std::optional<unsigned> com_number(const std::string& name) {
    if (name.size() < 4 || name.compare(0, 3, "COM") != 0) return std::nullopt;
    unsigned value{};
    const auto [end, error] = std::from_chars(name.data() + 3, name.data() + name.size(), value);
    if (error != std::errc{} || end != name.data() + name.size() || value == 0) return std::nullopt;
    return value;
}
std::set<unsigned> taken_numbers(const VirtualComMachine& machine) {
    std::set<unsigned> taken;
    for (std::size_t bit = 0; bit < machine.com_db.size() * 8; ++bit)
        if (machine.com_db[bit / 8] & (1u << (bit % 8))) taken.insert(static_cast<unsigned>(bit + 1));
    for (const auto& port : machine.active_ports) if (const auto n = com_number(port)) taken.insert(*n);
    for (const auto& pair : machine.pairs)
        for (const auto* port : {&pair.a, &pair.b}) if (const auto n = com_number(*port)) taken.insert(*n);
    return taken;
}
bool present(const VirtualComMachine& machine, const VirtualPair& owned) {
    return std::ranges::any_of(machine.pairs, [&](const VirtualPair& pair) {
        return pair.id == owned.id && pair.a == owned.a && pair.b == owned.b;
    });
}
// An Owned Pair whose id now carries other names may belong to someone else: never guess.
void require_consistent(const VirtualComMachine& machine, const VirtualComOwnership& ownership) {
    for (const auto& owned : ownership.pairs)
        for (const auto& pair : machine.pairs)
            if (pair.id == owned.id && (pair.a != owned.a || pair.b != owned.b))
                throw std::runtime_error("配對 " + std::to_string(pair.id) + " 的埠名稱已被改成 " + pair.a + "/" + pair.b +
                    "（本程式記錄為 " + owned.a + "/" + owned.b + "），為避免動到別人的配對，不做任何變更");
}
}
std::vector<VirtualPair> com0com_pairs(const std::map<std::string, std::string>& port_names,
    const std::vector<unsigned>& bus_ids) {
    // Without a PortName parameter com0com names the endpoint after its key.
    const auto endpoint = [&](const std::string& key) {
        const auto found = port_names.find(key);
        return found == port_names.end() ? key : found->second;
    };
    std::vector<VirtualPair> pairs;
    for (const auto id : std::set<unsigned>(bus_ids.begin(), bus_ids.end()))
        pairs.push_back({id, endpoint("CNCA" + std::to_string(id)), endpoint("CNCB" + std::to_string(id))});
    return pairs;
}
std::vector<PairRequest> plan_create(const VirtualComMachine& machine, const VirtualComOwnership& ownership) {
    if (!machine.driver_installed) throw std::runtime_error("尚未安裝 com0com 驅動，無法建立配對");
    require_consistent(machine, ownership);
    const auto owned_present = std::ranges::count_if(ownership.pairs,
        [&](const VirtualPair& owned) { return present(machine, owned); });
    auto taken = taken_numbers(machine);
    unsigned next = 10;  // Leave low numbers to physical adapters.
    const auto allocate = [&] {
        while (taken.contains(next)) ++next;
        taken.insert(next);
        return "COM" + std::to_string(next);
    };
    std::vector<PairRequest> plan;
    for (auto i = owned_present; i < 2; ++i) {
        auto a = allocate();
        auto b = allocate();
        plan.push_back({std::move(a), std::move(b)});
    }
    return plan;
}
std::vector<unsigned> plan_remove(const VirtualComMachine& machine, const VirtualComOwnership& ownership) {
    require_consistent(machine, ownership);
    std::vector<unsigned> ids;
    for (const auto& owned : ownership.pairs) if (present(machine, owned)) ids.push_back(owned.id);
    return ids;
}
UninstallPlan plan_uninstall(const VirtualComMachine& machine, const VirtualComOwnership& ownership) {
    if (!ownership.driver_installed_by_us)
        return {false, "com0com 驅動不是由本程式安裝，可能有其他程式在使用，請從 Windows「應用程式」自行移除", {}};
    std::string foreign;
    for (const auto& pair : machine.pairs) {
        const bool owned = std::ranges::any_of(ownership.pairs, [&](const VirtualPair& mine) {
            return mine.id == pair.id && mine.a == pair.a && mine.b == pair.b;
        });
        if (!owned) foreign += (foreign.empty() ? "" : "、") + pair.a + "↔" + pair.b;
    }
    if (!foreign.empty())
        return {false, "電腦上還有其他程式的配對（" + foreign + "），解除安裝會一併移除它們，所以保留驅動", {}};
    return {true, {}, plan_remove(machine, ownership)};
}
std::optional<EnvironmentRoles> environment_roles(const VirtualComMachine& machine, const VirtualComOwnership& ownership) {
    const auto active = [&](const std::string& port) { return std::ranges::find(machine.active_ports, port) != machine.active_ports.end(); };
    std::vector<VirtualPair> usable;
    for (const auto& owned : ownership.pairs)
        if (present(machine, owned) && active(owned.a) && active(owned.b)) usable.push_back(owned);
    if (usable.size() < 2) return std::nullopt;
    return EnvironmentRoles{usable[0].a, usable[0].b, usable[1].a, usable[1].b};
}
EnvironmentActions available_actions(const VirtualComMachine& machine, const VirtualComOwnership& ownership) {
    if (!machine.driver_installed) return {.install = true};
    EnvironmentActions actions;
    try {
        actions.create = !plan_create(machine, ownership).empty();
        actions.remove = !plan_remove(machine, ownership).empty();
    } catch (const std::runtime_error&) {
        return {};  // an Owned Pair was renamed elsewhere: change nothing
    }
    actions.test = environment_roles(machine, ownership).has_value();
    // The driver goes last, once this application's pairs are removed.
    actions.uninstall = !actions.remove && plan_uninstall(machine, ownership).allowed;
    return actions;
}
std::string format_ownership(const VirtualComOwnership& ownership) {
    std::string text = std::string("driver-installed-by-us=") + (ownership.driver_installed_by_us ? "1" : "0") + "\n";
    for (const auto& pair : ownership.pairs) text += "pair=" + std::to_string(pair.id) + " " + pair.a + " " + pair.b + "\n";
    return text;
}
VirtualComOwnership parse_ownership(std::string_view text) {
    VirtualComOwnership ownership;
    std::istringstream lines{std::string(text)};
    for (std::string line; std::getline(lines, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.starts_with("driver-installed-by-us=")) ownership.driver_installed_by_us = line.ends_with("=1");
        else if (line.starts_with("pair=")) {
            std::istringstream fields(line.substr(5));
            VirtualPair pair;
            if (!(fields >> pair.id >> pair.a >> pair.b)) throw std::runtime_error("擁有權記錄格式錯誤：" + line);
            ownership.pairs.push_back(std::move(pair));
        }
    }
    return ownership;
}
VirtualComOwnership import_legacy_ownership(std::string_view json) {
    // The record could only come from this application's own installer and Create flow.
    VirtualComOwnership ownership{{}, true};
    std::size_t position = 0;
    // Value of the next "key": either "quoted" or a bare number, as ConvertTo-Json writes them.
    const auto next_value = [&](std::string_view key) -> std::optional<std::string> {
        const auto at = json.find("\"" + std::string(key) + "\"", position);
        if (at == std::string_view::npos) return std::nullopt;
        auto start = json.find_first_not_of(" \t\r\n", json.find(':', at) + 1);
        if (start == std::string_view::npos) throw std::runtime_error("舊版擁有權記錄格式錯誤");
        const bool quoted = json[start] == '"';
        if (quoted) ++start;
        const auto end = quoted ? json.find('"', start) : json.find_first_of(",}\r\n", start);
        if (end == std::string_view::npos) throw std::runtime_error("舊版擁有權記錄格式錯誤");
        position = end;
        return std::string(json.substr(start, end - start));
    };
    while (const auto id = next_value("Id")) {
        const auto a = next_value("A"), b = next_value("B");
        VirtualPair pair;
        const auto [end, error] = std::from_chars(id->data(), id->data() + id->size(), pair.id);
        if (!a || !b || error != std::errc{} || end != id->data() + id->size())
            throw std::runtime_error("舊版擁有權記錄格式錯誤");
        pair.a = *a;
        pair.b = *b;
        ownership.pairs.push_back(std::move(pair));
    }
    return ownership;
}
}
