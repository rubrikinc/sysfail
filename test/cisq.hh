#include <mutex>
#include <string>
#include <sys/syscall.h>
#include <expected>
#include <stdexcept>

// C-isq is needed because libc wrappers often do a completely different thing
// than expectd. Initial version of this library had tests that had larger
// tolerances because of this. The test expected SYS_read to be called whereas
// libc had the file mmap-ed.
// This makes behavior predictable and hence tests become reliable.
// Impl still calls libc-wrappers, but of need be we can call dispatch syscalls
// directly like sysfail SIGSYS handler does, because this is built as a
// shared-lib too and its syscalls will be intercepted just like libc's.
struct TmpFile {
    const std::string path;
    std::mutex m;

    TmpFile();
    ~TmpFile();
       std::expected<std::string, std::runtime_error> read();

    std::expected<void, std::runtime_error> write(const std::string& content);
};
