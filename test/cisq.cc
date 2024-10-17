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

    std::expected<int, std::runtime_error> open_file(
        const std::string& path,
        int flags
    ) {
        auto fd = syscall(SYS_openat, AT_FDCWD, path.c_str(), flags);
        if (fd < 0) {
            return std::unexpected(std::runtime_error(
                Cisq::err_msg("Failed to open file", errno)));
        }
        return fd;
    }
}

std::runtime_error Cisq::err_msg(const char* msg, int op_errno) {
    return std::runtime_error(
        std::string(msg) + ", err: " + std::strerror(op_errno));
}

Cisq::TmpFile::TmpFile() : path(makeTempFile()) {}

Cisq::TmpFile::~TmpFile() {
    unlink(path.c_str());
}

std::expected<std::string, std::runtime_error> Cisq::TmpFile::read() {
    std::lock_guard<std::mutex> lock(m);
    auto fd = open_file(path, O_RDONLY);
    if (!fd.has_value()) {
        return std::unexpected(fd.error());
    }
    std::string content;
    char buffer[1024];
    auto bytes = syscall(SYS_read, fd.value(), buffer, sizeof(buffer));
    auto read_err = errno;
    syscall(SYS_close, fd.value());
    if (bytes < 0) {
        return std::unexpected(err_msg("Failed to read file", read_err));
    }
    content.append(buffer, bytes);
    return content;
}

std::expected<void, std::runtime_error> Cisq::TmpFile::write(
    const std::string& content
) {
    std::lock_guard<std::mutex> lock(m);
    auto fd = open_file(path, O_WRONLY | O_TRUNC);
    if (!fd.has_value()) {
        return std::unexpected(fd.error());
    }
    auto bytes = syscall(SYS_write, fd.value(), content.c_str(), content.size());
    auto write_err = errno;
    syscall(SYS_close, fd.value());
    if (bytes < 0) {
        return std::unexpected(err_msg("Failed to write file", errno));
    }
    return {};
}
