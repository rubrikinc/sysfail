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

#include <stdio.h>
#include <sysfail.hh>

using namespace std::chrono_literals;

int main(int argc, char** argv) {
  sysfail::Plan p(
    { {SYS_write, {0.5, 0, 0us, {{EINTR, 1.0}}}} },
    [](pid_t pid) { return true; },
    sysfail::thread_discovery::None{});

  sysfail::Session s(p);

  int fail_count = 0;

  for (int i = 0; i < 10; i++) {
    printf("%d/10: this works sometimes, not the other times!\n", i);
  }

  return 0;
}
