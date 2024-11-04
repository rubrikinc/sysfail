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

#include <gtest/gtest.h>
#include <expected>

#include "map.hh"

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