#include "environment/virtual_com.hpp"
#include <iostream>
#include <stdexcept>
namespace {
int failures = 0;
void check(bool ok, const char* behavior) {
    if (!ok) { std::cerr << "FAILED: " << behavior << '\n'; ++failures; }
}
posmv::VirtualComMachine machine_with_driver() {
    posmv::VirtualComMachine machine;
    machine.driver_installed = true;
    machine.com_db = {0x01};  // COM1 reserved
    machine.active_ports = {"COM1"};
    return machine;
}
// The Virtual COM Environment as this application leaves it after Create.
posmv::VirtualComMachine machine_with_environment() {
    auto machine = machine_with_driver();
    machine.pairs = {{0, "COM10", "COM11"}, {1, "COM12", "COM13"}};
    machine.com_db = {0x01, 0x1E};  // COM1, COM10-COM13 reserved
    machine.active_ports = {"COM1", "COM10", "COM11", "COM12", "COM13"};
    return machine;
}
posmv::VirtualComOwnership owned_environment() {
    return {{{0, "COM10", "COM11"}, {1, "COM12", "COM13"}}, true};
}
}
int main() {
    {
        const auto pairs = posmv::com0com_pairs({{"CNCA0", "COM10"}, {"CNCB0", "COM11"},
            {"CNCA1", "COM12"}, {"CNCB1", "COM13"}, {"ComDB", "ignored"}}, {0, 1});
        check(pairs.size() == 2 && pairs[0].id == 0 && pairs[0].a == "COM10" && pairs[0].b == "COM11" &&
            pairs[1].id == 1 && pairs[1].a == "COM12" && pairs[1].b == "COM13",
            "com0com endpoint parameters become Virtual Pairs");
    }
    {
        // setupc remove deletes the pair's devices but leaves its endpoint parameters behind.
        const auto pairs = posmv::com0com_pairs({{"CNCA0", "COM10"}, {"CNCB0", "COM11"},
            {"CNCA1", "COM12"}, {"CNCB1", "COM13"}}, {1});
        check(pairs.size() == 1 && pairs[0].id == 1 && pairs[0].a == "COM12" && pairs[0].b == "COM13",
            "parameters left by a removed pair are not a Virtual Pair");
    }
    {
        const auto plan = posmv::plan_create(machine_with_driver(), {});
        check(plan.size() == 2 && plan[0].a == "COM10" && plan[0].b == "COM11" &&
            plan[1].a == "COM12" && plan[1].b == "COM13",
            "create on a clean machine plans COM10<->COM11 and COM12<->COM13");
    }
    {
        auto machine = machine_with_driver();
        machine.com_db = {0x01, 0x0A};          // COM1, COM10 and COM12 reserved by others
        machine.active_ports = {"COM1", "COM14"};
        const auto plan = posmv::plan_create(machine, {});
        check(plan.size() == 2 && plan[0].a == "COM11" && plan[0].b == "COM13" &&
            plan[1].a == "COM15" && plan[1].b == "COM16",
            "create never takes a COM number that is reserved or present");
    }
    check(posmv::plan_create(machine_with_environment(), owned_environment()).empty(),
        "create plans nothing when both Owned Pairs are present");
    {
        auto machine = machine_with_environment();
        machine.pairs.pop_back();                 // second Owned Pair removed outside this application
        machine.com_db = {0x01, 0x06};            // its numbers were released
        machine.active_ports = {"COM1", "COM10", "COM11"};
        const auto plan = posmv::plan_create(machine, owned_environment());
        check(plan.size() == 1 && plan[0].a == "COM12" && plan[0].b == "COM13",
            "create replaces only the missing Owned Pair");
    }
    {
        auto machine = machine_with_driver();
        machine.driver_installed = false;
        bool refused = false;
        try { posmv::plan_create(machine, {}); } catch (const std::runtime_error&) { refused = true; }
        check(refused, "create is refused when the driver is not installed");
    }
    {
        auto machine = machine_with_environment();
        machine.pairs[0] = {0, "COM20", "COM21"};  // pair 0 renamed outside this application
        bool refused = false;
        try { posmv::plan_create(machine, owned_environment()); } catch (const std::runtime_error&) { refused = true; }
        check(refused, "create is refused when an Owned Pair's endpoints were renamed");
    }
    {
        auto machine = machine_with_environment();
        machine.pairs.push_back({2, "COM20", "COM21"});  // Foreign Pair
        check(posmv::plan_remove(machine, owned_environment()) == std::vector<unsigned>{0, 1},
            "remove targets only Owned Pairs");
    }
    {
        auto machine = machine_with_environment();
        machine.pairs[1] = {1, "COM30", "COM31"};
        bool refused = false;
        try { posmv::plan_remove(machine, owned_environment()); } catch (const std::runtime_error&) { refused = true; }
        check(refused, "remove is refused when an Owned Pair's endpoints were renamed");
    }
    {
        auto machine = machine_with_environment();
        machine.pairs.push_back({2, "COM20", "COM21"});  // Foreign Pair
        const auto plan = posmv::plan_uninstall(machine, owned_environment());
        check(!plan.allowed && !plan.reason.empty(), "uninstall is refused while a Foreign Pair exists");
    }
    {
        auto ownership = owned_environment();
        ownership.driver_installed_by_us = false;
        const auto plan = posmv::plan_uninstall(machine_with_environment(), ownership);
        check(!plan.allowed && !plan.reason.empty(), "uninstall is refused when this application did not install the driver");
    }
    {
        const auto plan = posmv::plan_uninstall(machine_with_environment(), owned_environment());
        check(plan.allowed && plan.remove_first == std::vector<unsigned>{0, 1},
            "uninstall removes the Owned Pairs first when allowed");
    }
    {
        const auto roles = posmv::environment_roles(machine_with_environment(), owned_environment());
        check(roles && roles->tx == "COM10" && roles->input == "COM11" && roles->output == "COM12" && roles->rx == "COM13",
            "two present Owned Pairs give TX, Input, Output and RX");
    }
    {
        auto machine = machine_with_environment();
        machine.active_ports = {"COM1", "COM10", "COM11"};  // second pair registered but its ports never appeared
        check(!posmv::environment_roles(machine, owned_environment()),
            "no roles while an Owned Pair's ports are absent from Windows");
    }
    {
        const auto loaded = posmv::parse_ownership(posmv::format_ownership(owned_environment()));
        check(loaded.driver_installed_by_us && loaded.pairs.size() == 2 &&
            loaded.pairs[0].id == 0 && loaded.pairs[0].a == "COM10" && loaded.pairs[0].b == "COM11" &&
            loaded.pairs[1].id == 1 && loaded.pairs[1].a == "COM12" && loaded.pairs[1].b == "COM13",
            "ownership reads back exactly as it was written");
    }
    {
        // Exact bytes left on the development machine by the PowerShell manager (BOM, CRLF).
        const auto legacy = posmv::import_legacy_ownership("\xEF\xBB\xBF[\r\n    {\r\n        \"Id\":  \"0\",\r\n"
            "        \"A\":  \"COM10\",\r\n        \"B\":  \"COM11\"\r\n    },\r\n    {\r\n        \"Id\":  \"1\",\r\n"
            "        \"A\":  \"COM12\",\r\n        \"B\":  \"COM13\"\r\n    }\r\n]\r\n");
        check(legacy.driver_installed_by_us && legacy.pairs.size() == 2 &&
            legacy.pairs[0].id == 0 && legacy.pairs[0].a == "COM10" && legacy.pairs[0].b == "COM11" &&
            legacy.pairs[1].id == 1 && legacy.pairs[1].a == "COM12" && legacy.pairs[1].b == "COM13",
            "legacy PowerShell ownership imports its pairs as installed by this application");
    }
    return failures ? 1 : 0;
}
