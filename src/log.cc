#include <sys/syscall.h>
#include <cstring>
#include <unistd.h>

#include "log.hh"
#include "syscall.hh"

void sysfail::log(const char* msg) {
    syscall(STDERR_FILENO, (long)(msg), strlen(msg), 0, 0, 0, SYS_write);
}

void sysfail::log(const char* msg, long arg1) {
    char buffer[256];
    int n = snprintf(buffer, sizeof(buffer), msg, arg1);
    syscall(STDERR_FILENO, (long)(buffer), n, 0, 0, 0, SYS_write);
}
