#pragma once

#include <cstdint>
#include <vector>

//! Re-encodes any TGA (uncompressed or RLE, 24/32 bit, any origin) as a plain
//! uncompressed 32 bit TGA with every read bounds checked, see TgaUtils.cpp.
bool sanitizeTga(const uint8_t *in, uint32_t inSize, std::vector<uint8_t> &out);
