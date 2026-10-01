#pragma once
namespace posmv {
// `posmv-simulator env <inspect|install|create|test|remove|uninstall> --result FILE [--legacy FILE]`.
// EnvironmentManager starts install/create/remove/uninstall through UAC; the worker writes FILE
// as `status=`, `message=` and `roles=` lines for the Simulator to display.
int run_virtual_com_worker();
}
