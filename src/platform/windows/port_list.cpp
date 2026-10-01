#include "transport/port_list.hpp"
#include <windows.h>
#include <string>
#include <vector>
namespace posmv {
std::vector<std::string> available_ports() {
    std::vector<std::string> ports;
    char target[4096];
    for (unsigned i = 1; i <= 4096; ++i) {
        auto name = "COM" + std::to_string(i);
        if (QueryDosDeviceA(name.c_str(), target, sizeof(target))) ports.push_back(name);
    }
    return ports;
}
}