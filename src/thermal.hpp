#pragma once
// thermal.hpp
// reads Ryzen Tdie via Linux hwmon sysfs

#include <cstdint>
#include <string>
#include <vector>

namespace rsb::Thermal {

struct Reading {
    double tdie   = 0.0;
    double tctl   = 0.0;
    double socket_w = 0.0; // from k10temp power node if available
};

// returns Tdie in Celsius, or -1.0 on failure
double read_tdie();

Reading read_all();

// returns path to k10temp hwmon dir, e.g. /sys/class/hwmon/hwmon2
std::string find_k10temp_path();

} // namespace rsb::Thermal
