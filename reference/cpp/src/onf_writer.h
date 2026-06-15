#pragma once

#include <string>
#include "onf_types.h"

// Writes a Document to a .onf file at the given path.
// Returns true on success, false on failure.
bool write_onf(const Document& doc, const std::string& output_path);