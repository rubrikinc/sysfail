#include <fcntl.h>
#include <unistd.h>
#include <expected>
#include <stdexcept>
#include <cassert>
#include <cstring>

#include "cisq.hh"

namespace {
    std::string makeTempFile() {
        char path[] = "/tmp/sysfail-XXXXXX";
        int fd = mkstemp(path);
        if (fd == -1) {
            throw std::runtime_error("Failed to create temp file");
        }
        close(fd);
        return path;
    }

    std::runtime_error err_msg(const char* msg, long sys_return) {
        assert(sys_return < 0);
        return std::runtime_error(
            std::string(msg) + ", err: " + std::strerror(-sys_return));
    }

    std::expected<int, std::runtime_error> open_file(
        const std::string& path,
        int flags
    ) {
        auto fd = syscall(SYS_openat, AT_FDCWD, path.c_str(), flags);
        if (fd < 0) {
            return std::unexpected(std::runtime_error(
                err_msg("Failed to open file", fd)));
        }
        return fd;
    }
}

TmpFile::TmpFile() : path(makeTempFile()) {}

TmpFile::~TmpFile() {
    unlink(path.c_str());
}

std::expected<std::string, std::runtime_error> TmpFile::read() {
    std::lock_guard<std::mutex> lock(m);
    auto fd = open_file(path, O_RDONLY);
    if (!fd.has_value()) {
        return std::unexpected(fd.error());
    }
    std::string content;
    char buffer[1024];
    auto bytes = syscall(SYS_read, fd.value(), buffer, sizeof(buffer));
    syscall(SYS_close, fd.value());
    if (bytes < 0) {
        return std::unexpected(err_msg("Failed to read file", bytes));
    }
    content.append(buffer, bytes);
    return content;
}

std::expected<void, std::runtime_error> TmpFile::write(
    const std::string& content
) {
    std::lock_guard<std::mutex> lock(m);
    auto fd = open_file(path, O_WRONLY | O_TRUNC);
    if (!fd.has_value()) {
        return std::unexpected(fd.error());
    }
    auto bytes = syscall(SYS_write, fd.value(), content.c_str(), content.size());
    syscall(SYS_close, fd.value());
    if (bytes < 0) {
        return std::unexpected(err_msg("Failed to write file", bytes));
    }
    return {};
}
