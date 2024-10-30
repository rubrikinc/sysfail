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
