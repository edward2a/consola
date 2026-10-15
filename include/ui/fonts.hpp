// SPDX-License-Identifier: MIT

#pragma once

namespace consola {

// Register the bundled preferred font (Inconsolata Condensed) and the embedded
// fallback font (DejaVu Sans Mono) with fontconfig, and make them visible to
// Xft's font matching. Call once, before creating any terminal widget.
void register_application_fonts();

} // namespace consola
