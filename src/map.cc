#include "map.hh"

std::optional<std::map<uintptr_t, MemoryMapInfo>> get_mmap(pid_t pid) {
    std::map<uintptr_t, MemoryMapInfo> mem_map;
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

        MemoryMapInfo info;
        info.length = endAddr - startAddr;
        info.permissions = permissions;
        info.path = path;
        info.inode = inode;

        mem_map[startAddr] = info;
    }

    return mem_map;
}