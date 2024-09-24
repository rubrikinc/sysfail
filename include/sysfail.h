extern "C" {
    struct sysfail_plan_t {
    };
    struct sysfail_session_t {
        void* data;
        bool (*stop) (void*);
    };
    sysfail_session_t* start(const sysfail_plan_t *);
}