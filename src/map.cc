#include "map.hh"

#include <regex>

std::optional<sysfail::Mapping> sysfail::get_mmap(pid_t pid) {
    Mapping mapping;
    std::string filePath = "/proc/" + std::to_string(pid) + "/maps";
    std::ifstream mapsFile(filePath);

    if (!mapsFile.is_open()) {
        std::cerr << "Failed to open " << filePath << "\n";
        return std::nullopt;
    }

    std::string line;
    while (std::getline(mapsFile, line)) {
        std::istringstream iss(line);
        uintptr_t startAddr, endAddr;
        std::string permissions, offsetStr, devStr, path = "";
        uintptr_t inode;

        iss >> std::hex >> startAddr;
        iss.ignore(1, '-');
        iss >> std::hex >> endAddr;
        iss >> permissions >> offsetStr >> devStr >> inode;

        std::getline(iss, path);
        // Trim leading spaces
        if (!path.empty()) {
            path.erase(0, path.find_first_not_of(' '));
        }

        AddrRange info;
        info.start = startAddr;
        info.length = endAddr - startAddr;
        info.permissions = permissions;
        info.path = path;
        info.inode = inode;

        mapping.map[startAddr] = info;
    }

    return mapping;
}

bool sysfail::AddrRange::executable() const {
    return permissions.find("x") != std::string::npos;
}

bool sysfail::AddrRange::vdso() const {
    std::regex vdsoRe(R"(^\[[a-zA-Z0-9]+\]$)");
    return std::regex_match(path, vdsoRe);
}

bool sysfail::AddrRange::libsysfail() const {
    std::regex soRegex(R"(^.*/libsysfail[.0-9]+*\.so[.0-9]*$)");
    return std::regex_match(path, soRegex);
}

std::vector<sysfail::AddrRange> sysfail::Mapping::bypass_mappings() {
    std::vector<AddrRange> mappings;
    for (const auto& [_, info] : map) {
        if (info.executable() && info.libsysfail()) {
            mappings.push_back(info);
        }
    }
    return mappings;
}