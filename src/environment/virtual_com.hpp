#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace posmv {
struct VirtualPair { unsigned id{}; std::string a, b; };  // Endpoint port names
// Facts read from Windows by a driver adapter; this module makes no OS calls.
struct VirtualComMachine {
    bool driver_installed{};
    std::vector<VirtualPair> pairs;          // every Virtual Pair on the machine, Owned or Foreign
    std::vector<std::uint8_t> com_db;        // COM Name Arbiter bitmap: bit n reserves COM(n + 1)
    std::vector<std::string> active_ports;   // ports currently present (SERIALCOMM)
};
struct VirtualComOwnership { std::vector<VirtualPair> pairs; bool driver_installed_by_us{}; };
struct PairRequest { std::string a, b; };
// com0com keeps endpoint names under CNCA<id> and CNCB<id>; a pair exists while its bus device
// (whose PortNum is the id) exists, even if the driver failed to load.
std::vector<VirtualPair> com0com_pairs(const std::map<std::string, std::string>& port_names,
    const std::vector<unsigned>& bus_ids);
// Pairs to install so that two Owned Pairs exist; never reuses a reserved or present COM number.
std::vector<PairRequest> plan_create(const VirtualComMachine& machine, const VirtualComOwnership& ownership);
// Ids of Owned Pairs present on the machine; Foreign Pairs are never included.
std::vector<unsigned> plan_remove(const VirtualComMachine& machine, const VirtualComOwnership& ownership);
struct UninstallPlan { bool allowed{}; std::string reason; std::vector<unsigned> remove_first; };
// The driver may go only if this application installed it and no Foreign Pair depends on it.
UninstallPlan plan_uninstall(const VirtualComMachine& machine, const VirtualComOwnership& ownership);
struct EnvironmentRoles { std::string tx, input, output, rx; };
// Simulator TX -> Proxy Input on the first Owned Pair, Proxy Output -> Simulator RX on the second.
std::optional<EnvironmentRoles> environment_roles(const VirtualComMachine& machine, const VirtualComOwnership& ownership);
std::string format_ownership(const VirtualComOwnership& ownership);
VirtualComOwnership parse_ownership(std::string_view text);
// owned.json written by the retired PowerShell manager (per-user, before ownership moved machine-wide).
VirtualComOwnership import_legacy_ownership(std::string_view owned_json);
}
