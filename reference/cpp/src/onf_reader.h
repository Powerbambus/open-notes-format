#pragma once

#include <string>
#include "onf_types.h"

// Reads a .onf file from the given path into a Document struct.
// Returns true on success, false on failure.
bool read_onf(const std::string& input_path, Document& doc);