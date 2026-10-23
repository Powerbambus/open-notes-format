#pragma once

#include <cstdint>
#include <string>
#include <vector>

// SHA-256 as defined in FIPS 180-4. Asset files are named by this hash (Spec Section 9.2.1).
// Returns the digest as 64 lowercase hexadecimal digits.
std::string sha256_hex(const std::vector<uint8_t>& data);
