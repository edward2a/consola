// SPDX-License-Identifier: MIT

#pragma once

#include <string_view>

namespace consola {

// Name of the profile new panes use when none is selected (DIP 5.2). Shared by
// the configuration model and saved-layout data.
inline constexpr std::string_view kDefaultProfileName = "default";

} // namespace consola
