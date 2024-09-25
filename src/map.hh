#ifndef _MAP_HH
#define _MAP_HH

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <optional>
#include <vector>
#include <stdint.h>

namespace sysfail {
    struct AddrRange {
        uintptr_t start;
        uintptr_t length;
        std::string permissions;
        std::string path;
        uintptr_t inode;

        bool executable() const;
        bool vdso() const;
        bool libsysfail() const;
    };

    struct Mapping {
        std::map<uintptr_t, AddrRange> map;

        std::vector<AddrRange> bypass_mappings();
    };

    std::optional<Mapping> get_mmap(pid_t pid);
}

#endif