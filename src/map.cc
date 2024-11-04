/*
 * Copyright © 2024 Rubrik, Inc. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "map.hh"

#include <regex>
#include <cassert>

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

sysfail::AddrRange sysfail::Mapping::self_text() {
    std::vector<AddrRange> mappings;
    for (const auto& [_, info] : map) {
        if (info.executable() && info.libsysfail()) {
            mappings.push_back(info);
        }
    }
    assert(mappings.size() == 1);

    return mappings[0];
}