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

#include <sys/syscall.h>
#include <cstring>
#include <unistd.h>
#include <cstdarg>
#include <cstdio>  

#include "log.hh"
#include "syscall.hh"


void sysfail::log(const char* msg, ...) {
    const int len = 512;
    char buffer[len];
    va_list args;
    va_start(args, msg);
    int n = vsnprintf(buffer, sizeof(buffer), msg, args);
    va_end(args);
    // Ensure the write length does not exceed the buffer size
    if (n > len) {
        n = len;
    }
    syscall(STDERR_FILENO, (long)(buffer), n, 0, 0, 0, SYS_write);
}
