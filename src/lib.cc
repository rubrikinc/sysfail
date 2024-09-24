#include <iostream>

#include "sysfail.hh"
#include "sysfail.h"
#include "map.hh"

sysfail::Session::Session(const Plan& _plan) {
    std::cout << "sysfail::Session::Session(const &)" << std::endl;

     auto memoryMapOpt = get_mmap(getpid());
    if (!memoryMapOpt) {
        throw std::runtime_error("Failed to get memory map");
    }

    const auto& memoryMap = *memoryMapOpt;
    for (const auto& entry : memoryMap) {
        std::cout << "Start Address: " << std::hex << entry.first
                  << "\nLength: " << std::dec << entry.second.length
                  << "\nPermissions: " << entry.second.permissions
                  << "\nPath: " << entry.second.path
                  << "\nInode: " << entry.second.inode
                  << "\n\n";
    }
}

bool sysfail::Session::stop() {
    std::cout << "sysfail::Session::stop()" << std::endl;
    return true;
}

extern "C" {
    sysfail_session_t* start(const sysfail_plan_t *plan) {
        auto session = new sysfail::Session({});
        auto stop = [](void* data) {
                return static_cast<sysfail::Session*>(data)->stop();
            };
        return new sysfail_session_t{session, stop};
    }
}
