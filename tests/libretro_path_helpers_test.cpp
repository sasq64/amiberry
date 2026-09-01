#include <iostream>
#include <string>

#include "libretro/libretro_path_helpers.h"

static int failures;

static void expect_eq(const std::string& actual, const std::string& expected, const char* message)
{
	if (actual != expected) {
		std::cerr << message << ": expected '" << expected << "', got '" << actual << "'\n";
		failures++;
	}
}

// The `windows` argument is passed explicitly throughout so both conventions
// are covered whichever host the tests run on.
static void test_strip_verbatim_prefix()
{
	// The form a frontend that canonicalises hands over, and the one opendir()
	// cannot walk. This is what left the ROM scan empty and the screen black.
	expect_eq(libretro_strip_verbatim_prefix(R"(\\?\C:\demarc\system\amiga)", true),
		R"(C:\demarc\system\amiga)", "drive-letter verbatim path");
	expect_eq(libretro_strip_verbatim_prefix(R"(\\?\UNC\server\share\roms)", true),
		R"(\\server\share\roms)", "verbatim UNC path");

	// Anything else is returned untouched.
	expect_eq(libretro_strip_verbatim_prefix(R"(C:\demarc\system\amiga)", true),
		R"(C:\demarc\system\amiga)", "plain windows path");
	expect_eq(libretro_strip_verbatim_prefix(R"(\\server\share)", true),
		R"(\\server\share)", "plain UNC path");
	expect_eq(libretro_strip_verbatim_prefix("", true), "", "empty path");

	// Off Windows the prefix is not special and may legitimately start a name.
	expect_eq(libretro_strip_verbatim_prefix(R"(\\?\C:\demarc)", false),
		R"(\\?\C:\demarc)", "prefix is not special on unix");
	expect_eq(libretro_strip_verbatim_prefix("/home/sasq/system/amiga", false),
		"/home/sasq/system/amiga", "unix path");
}

static void test_path_join()
{
	// A verbatim path rejects '/', so the separator has to be the native one.
	expect_eq(libretro_path_join(R"(C:\demarc\system\amiga)", "kick34005.A500", true),
		R"(C:\demarc\system\amiga\kick34005.A500)", "windows join");
	expect_eq(libretro_path_join("/home/sasq/amiga", "kick34005.A500", false),
		"/home/sasq/amiga/kick34005.A500", "unix join");

	// A separator already there is not doubled up, in either convention.
	expect_eq(libretro_path_join(R"(C:\demarc\amiga\)", "ROMs", true),
		R"(C:\demarc\amiga\ROMs)", "windows join keeps single separator");
	expect_eq(libretro_path_join("/home/sasq/amiga/", "ROMs", false),
		"/home/sasq/amiga/ROMs", "unix join keeps single separator");
	expect_eq(libretro_path_join(R"(C:\demarc\amiga/)", "ROMs", true),
		R"(C:\demarc\amiga/ROMs)", "windows join accepts a trailing slash");

	// An empty half yields the other one rather than a stray separator.
	expect_eq(libretro_path_join("", "ROMs", true), "ROMs", "empty dir");
	expect_eq(libretro_path_join(R"(C:\demarc)", "", true), R"(C:\demarc)", "empty file");
}

int main()
{
	test_strip_verbatim_prefix();
	test_path_join();

	if (failures > 0) {
		std::cerr << failures << " libretro path helper test(s) failed\n";
		return 1;
	}
	std::cout << "libretro path helper tests passed\n";
	return 0;
}
