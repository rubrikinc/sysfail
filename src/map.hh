#ifndef _MAP_HH
#define _MAP_HH

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <optional>
#include <stdint.h>

struct MemoryMapInfo {
    uintptr_t length;
    std::string permissions;
    std::string path;
    uintptr_t inode;
};

std::optional<std::map<uintptr_t, MemoryMapInfo>> get_mmap(pid_t pid);

#endif