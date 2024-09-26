#include <gtest/gtest.h>
#include <sysfail.hh>
#include <expected>

using namespace testing;

namespace sysfail {
	std::string makeTempFile() {
		char path[] = "/tmp/sysfail-XXXXXX";
		int fd = mkstemp(path);
		if (fd == -1) {
			throw std::runtime_error("Failed to create temp file");
		}
		close(fd);
		return path;
	}

	struct TmpFile {
		const std::string path;
		TmpFile() : path(makeTempFile()) {}
		~TmpFile() {
			unlink(path.c_str());
		}

		void write(const std::string& content) {
			FILE* file = fopen(path.c_str(), "w");
			if (!file) {
				throw std::runtime_error("Failed to open file");
			}
			if (file) {
				fwrite(content.c_str(), 1, content.size(), file);
				if (ferror(file)) {
					fclose(file);
					throw std::runtime_error("Failed to write file");
				}
				fclose(file);
			}
		}

		std::expected<std::string, std::runtime_error> read() {
    		std::string content;
    		FILE* file = fopen(path.c_str(), "r");
    		if (!file) {
    			return std::unexpected(std::runtime_error("Failed to open file"));
    		}
    		if (file) {
    			char buffer[1024];
    			while (size_t len = fread(buffer, 1, sizeof(buffer), file)) {
    				content.append(buffer, len);
    			}
    			if (ferror(file)) {
    				fclose(file);
    				return std::unexpected(std::runtime_error("Failed to read file"));
    			}
    			fclose(file);
    		}
    		return content;
    	}
	};

	TEST(SysFail, LoadSessionWithoutFailureInjection) {
		TmpFile tFile;
		tFile.write("foo bar baz quux");

		sysfail::Session s({});
		auto success = 0;
		for (int i = 0; i < 10; i++) {
			auto r = tFile.read();
			if (r.has_value()) {
				success++;
				EXPECT_EQ(r.value(), "foo bar baz quux");
			}
		}
		EXPECT_EQ(success, 10);
		s.stop();
	}

	TEST(SysFail, LoadSessionWithSysReadBlocked) {
		TmpFile tFile;
		tFile.write("foo bar baz quux");

		sysfail::Plan p(
			{
				{SYS_read, {1.0, 0, std::chrono::microseconds(0), {{EIO, 1.0}}}}
			},
			[](pid_t pid) {
				return true;
			}
		);

		sysfail::Session s(p);
		auto success = 0;
		for (int i = 0; i < 10; i++) {
		auto r = tFile.read();
			if (r.has_value()) {
				success++;
				EXPECT_EQ(r.value(), "foo bar baz quux");
			}
		}
		EXPECT_EQ(success, 0);
		s.stop();
	}

	TEST(SysFail, SysOpenAndReadFailureInjection) {
		TmpFile tFile;
		tFile.write("foo bar baz quux");

		sysfail::Plan p(
			{
				{SYS_read, {0.33, 0, std::chrono::microseconds(0), {{EIO, 1.0}}}},
				{SYS_openat, {0.25, 0, std::chrono::microseconds(0), {{EINVAL, 1.0}}}}
			},
			[](pid_t pid) {
				return true;
			}
		);

		sysfail::Session s(p);
		auto success = 0;
		for (int i = 0; i < 1000; i++) {
			auto r = tFile.read();
			if (r.has_value()) {
				success++;
			}
		}
		// 50 +- 25% (margin of error) around mean 50% expected success rate
		// Read happens after open
		// P(open succeeds) = (1 - 0.25) = 0.75
		// P(read success | open success) = 0.67
		// P(read success) = P(read success | open success) * P (open success) =
		//     0.67 * 0.75 = 0.50
		EXPECT_GT(success, 250);
		EXPECT_LT(success, 750);
		s.stop();

		success = 0;
		for (int i = 0; i < 100; i++) {
			auto r = tFile.read();
			if (r.has_value()) {
				success++;
			}
		}
		EXPECT_EQ(success, 100);
	}
}