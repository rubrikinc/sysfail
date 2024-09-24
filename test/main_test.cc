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
		sysfail::Session s({});
		s.stop();
		std::cout << "Dummy test" << std::endl;
	}

	TEST(SysFail, LoadSessionWithFailureInjection) {
		TmpFile tFile;
		tFile.write("foo bar baz quux");

		sysfail::Plan p(
			{
				{SYS_open, {1.0, 0, std::chrono::microseconds(0)}},
				{SYS_read, {1.0, 0, std::chrono::microseconds(0)}}
			},
			[](pid_t pid) { return true; }
		);

		sysfail::Session s(p);
		auto r = tFile.read();
		EXPECT_FALSE(r.has_value());
		s.stop();
		std::cout << "Dummy test" << std::endl;
	}
}