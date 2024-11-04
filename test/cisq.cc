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

#include <fcntl.h>
#include <unistd.h>
#include <expected>
#include <stdexcept>
#include <cassert>
#include <cstring>
#include <format>
#include <exception>
#include <functional>
#include <sys/timex.h>

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

std::expected<
    std::chrono::system_clock::time_point,
    Cisq::Err
> Cisq::tm_adjtimex() {
    // libc `adjtimex` wrapper doesn't appear to call the syscall at all,
    // use lib'c wrapper to directly call it.
    struct timex t{0};

    auto ret = syscall(SYS_adjtimex, &t);
    if (ret == -1) {
        return std::unexpected(Cisq::Err("adjtimex failed", errno));
    }

    using ns = std::chrono::nanoseconds;
    using us = std::chrono::microseconds;
    using s = std::chrono::seconds;

    return std::chrono::system_clock::time_point(
        s(t.time.tv_sec) +
        (t.status & STA_NANO ? ns(t.time.tv_usec) : us(t.time.tv_usec)));
}