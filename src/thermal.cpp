// thermal.cpp
#include "thermal.hpp"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace rsb::Thermal {

std::string find_k10temp_path() {
    const fs::path hwmon_root = "/sys/class/hwmon";
    std::error_code ec;

    for (auto& entry : fs::directory_iterator(hwmon_root, ec)) {
        auto name_file = entry.path() / "name";
        std::ifstream f(name_file);
        if (!f) continue;
        std::string name;
        std::getline(f, name);
        if (name == "k10temp") return entry.path().string();
    }
    return {};
}

static double read_millidegree(const std::string& path) {
    std::ifstream f(path);
    if (!f) return -1.0;
    int64_t val = 0;
    f >> val;
    return static_cast<double>(val) / 1000.0;
}

Reading read_all() {
    static const std::string base = find_k10temp_path();
    if (base.empty()) return {};

    Reading r;
    // Tdie is typically temp2, Tctl is temp1 on k10temp for Zen 5
    r.tctl = read_millidegree(base + "/temp1_input");
    r.tdie = read_millidegree(base + "/temp2_input");

    // power1_input in microwatts (not always present)
    std::ifstream pw(base + "/power1_input");
    if (pw) {
        uint64_t uw = 0;
        pw >> uw;
        r.socket_w = static_cast<double>(uw) / 1e6;
    }
    return r;
}

double read_tdie() {
    return read_all().tdie;
}

} // namespace rsb::Thermal
