#ifndef _HELPERS_HH
#define _HELPERS_HH

#include <filesystem>

template<class... Ts> struct cases : Ts... {
    using Ts::operator()...;
};

template<class... Ts> cases(Ts...) -> cases<Ts...>;

namespace sysfail {
    const std::filesystem::path tasks_dir =
        std::filesystem::path("/proc/self/task");
}

#endif