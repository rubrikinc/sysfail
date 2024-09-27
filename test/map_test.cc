#include <gtest/gtest.h>
#include <map.hh>
#include <expected>

using namespace testing;

namespace sysfail {
	TEST(Map, RecognizesMappingTypes) {
		AddrRange range;
		range.permissions = "r-xp";
		ASSERT_TRUE(range.executable());
		ASSERT_FALSE(range.vdso());
		ASSERT_FALSE(range.libsysfail());

		range.permissions = "r--p";
		ASSERT_FALSE(range.executable());
		ASSERT_FALSE(range.vdso());
		ASSERT_FALSE(range.libsysfail());

		range.permissions = "r-xp";
		range.path = "[vdso]";
		ASSERT_TRUE(range.executable());
		ASSERT_TRUE(range.vdso());
		ASSERT_FALSE(range.libsysfail());

		range.permissions = "r-xp";
		range.path = "/libsysfail.so";
		ASSERT_TRUE(range.executable());
		ASSERT_FALSE(range.vdso());
		ASSERT_TRUE(range.libsysfail());

		range.path = "/libsysfail.3.2.so.6.1";
		ASSERT_TRUE(range.libsysfail());

		range.path = "/libsysfail.3.so.6";
		ASSERT_TRUE(range.libsysfail());
	}

	TEST(Map, CanFilterExecutableMappings) {
		auto m = get_mmap(getpid());
        ASSERT_TRUE(m.has_value());

		auto mappings = m->self_text();

		ASSERT_TRUE(mappings.executable());

		ASSERT_TRUE(mappings.path.find("libsysfail.so") != std::string::npos);
	}
}