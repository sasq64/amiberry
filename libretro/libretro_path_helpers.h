#ifndef LIBRETRO_PATH_HELPERS_H
#define LIBRETRO_PATH_HELPERS_H

#include <string>

// Whether this build's host filesystem uses Windows path conventions. The
// helpers below take it as a defaulted argument so the tests can exercise both
// conventions from either host.
inline constexpr bool libretro_windows_paths =
#ifdef _WIN32
	true;
#else
	false;
#endif

// Strip Windows' `\\?\` extended-length path prefix.
//
// Such a path goes to the object manager unparsed: `/` is not a separator
// there, `.` and `..` are not resolved, and the CRT's directory calls fail on
// one outright — `opendir()` included, which is what the ROM scan walks its
// candidate directories with. A frontend that hands its system directory over
// after canonicalising it with a modern API (Rust's `fs::canonicalize`, .NET's
// `Path.GetFullPath`) passes exactly this form down, and the scan then finds no
// Kickstart at all: the machine boots romless and renders a black screen.
//
// Strip it on ingestion so the rest of the core only ever sees an ordinary
// `C:\...` path. A verbatim UNC path (`\\?\UNC\server\share`) becomes
// `\\server\share`. No-op on a path without the prefix, and on hosts where the
// prefix is not special and could legitimately begin a filename.
inline std::string libretro_strip_verbatim_prefix(const std::string& path,
	const bool windows = libretro_windows_paths)
{
	if (!windows || path.rfind("\\\\?\\", 0) != 0)
		return path;

	const std::string rest = path.substr(4);
	if (rest.rfind("UNC\\", 0) == 0)
		return "\\\\" + rest.substr(4);
	return rest;
}

// Join `file` onto directory `dir` with the host's separator.
//
// The separator has to be the native one: a verbatim path rejects `/`, and
// while Win32 accepts it elsewhere, mixing the two produces paths that are
// awkward to log and to compare. Returns the other half when either is empty,
// and does not double up a separator `dir` already ends with.
inline std::string libretro_path_join(const std::string& dir, const std::string& file,
	const bool windows = libretro_windows_paths)
{
	if (dir.empty())
		return file;
	if (file.empty())
		return dir;

	const char last = dir.back();
	if (last == '/' || last == '\\')
		return dir + file;
	return dir + (windows ? '\\' : '/') + file;
}

#endif
