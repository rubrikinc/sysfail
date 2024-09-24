#include <iostream>

#include "sysfail.hh"

bool sysfail::Session::stop() {
    std::cout << "sysfail::Session::stop()" << std::endl;
    return true;
}

extern "C" {
    sysfail_session_t* start(const sysfail_plan_t *plan) {
        auto session = new sysfail::Session();
        auto stop = [](void* data) {
                return static_cast<sysfail::Session*>(data)->stop();
            };
        return new sysfail_session_t{session, stop};
    }
}
