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

        AddrRange self_text();
    };

    std::optional<Mapping> get_mmap(pid_t pid);
}

#endif