#pragma once

#include <string>

namespace surl {

/// Full `--help` text.
std::string help_text();

/// Short usage line printed when an argument is wrong.
std::string usage_line();

/// `--version` output. When @p verbose, includes the build details that
/// `surl doctor` and bug reports need.
std::string version_text(bool verbose);

} // namespace surl
