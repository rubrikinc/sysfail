#include <fcntl.h>
#include <unistd.h>
#include <expected>
#include <stdexcept>
#include <cassert>
#include <cstring>
#include <format>
#include <exception>
#include <functional>

#include "cisq.hh"
#include "log.hh"

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

    std::expected<int, Cisq::Err> open_file(
        const std::string& path,
        int flags
    ) {
        auto fd = syscall(SYS_openat, AT_FDCWD, path.c_str(), flags);
        if (fd < 0) {
            return std::unexpected(Cisq::Err("Failed to open file", errno));
        }
        return fd;
    }
}

std::string Cisq::err_msg(const char* msg, int op_errno) {
    return std::format("{}, err: {}", msg, std::strerror(op_errno));
}

Cisq::TmpFile::TmpFile() : path(makeTempFile()) {}

Cisq::TmpFile::~TmpFile() {
    unlink(path.c_str());
}

std::expected<std::string, Cisq::Err> Cisq::TmpFile::read() {
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
        return std::unexpected(Cisq::Err("Failed to read file", read_err));
    }
    content.append(buffer, bytes);
    return content;
}

std::expected<void, Cisq::Err> Cisq::TmpFile::write(
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
        return std::unexpected(Err("Failed to write file", errno));
    }
    return {};
}

Cisq::Err::Err(
    const char* msg,
    int op_errno
): std::runtime_error(msg), op_errno(op_errno) {}

Cisq::Err& Cisq::Err::operator=(const Err& other) {
    std::runtime_error::operator=(other);
    op_errno = other.op_errno;
    return *this;
}

int Cisq::Err::err() const {
    return op_errno;
}

static std::expected<void, Cisq::Err> change_flag(
    int fd,
    std::function<int(int)> f
) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return std::unexpected(Cisq::Err("fcntl F_GETFL", errno));
    }

    flags = f(flags);

    if (fcntl(fd, F_SETFL, flags) == -1) {
        return std::unexpected(Cisq::Err("fcntl F_SETFL", errno));
    }

    return {};
}

Cisq::AsyncRead::AsyncRead(const int fd): fd(fd) {
    auto ret = change_flag(fd, [](int flags) { return flags | O_NONBLOCK; });
    if (!ret) {
        throw ret.error();
    }
}

Cisq::AsyncRead::~AsyncRead() {
    auto ret = change_flag(fd, [](int flags) { return flags & ~O_NONBLOCK; });
    if (!ret) {
        sysfail::log(ret.error().what());
        std::terminate();
    }
}
