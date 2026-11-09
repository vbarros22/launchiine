#include "TgaUtils.h"

//! Re-encodes any TGA (uncompressed or RLE, 24/32 bit, any origin) as a plain
//! uncompressed 32 bit TGA with every read bounds checked. libgd's TGA reader (the one
//! libgui uses) can crash or hang on RLE or unusual files, so it only ever sees the
//! simple case. Returns false for files that are not valid icons.
bool sanitizeTga(const uint8_t *in, uint32_t inSize, std::vector<uint8_t> &out) {
    if (in == nullptr || inSize < 18) {
        return false;
    }
    uint32_t idLength     = in[0];
    uint32_t colorMapType = in[1];
    uint32_t imageType    = in[2];
    uint32_t cmapLength   = in[5] | (in[6] << 8);
    uint32_t cmapEntryBit = in[7];
    uint32_t width        = in[12] | (in[13] << 8);
    uint32_t height       = in[14] | (in[15] << 8);
    uint32_t bpp          = in[16];
    uint32_t descriptor   = in[17];

    if (imageType != 2 && imageType != 10) {
        return false; // only true color, raw or RLE
    }
    if (bpp != 24 && bpp != 32) {
        return false;
    }
    if (width == 0 || height == 0 || width > 1024 || height > 1024) {
        return false;
    }

    uint32_t offset = 18 + idLength;
    if (colorMapType == 1) {
        offset += cmapLength * ((cmapEntryBit + 7) / 8);
    }
    if (offset > inSize) {
        return false;
    }

    const uint32_t srcBytes   = bpp / 8;
    const uint32_t pixelCount = width * height;

    out.assign(18 + pixelCount * 4, 0);
    uint8_t *hdr = out.data();
    hdr[2]       = 2; // uncompressed true color
    hdr[12]      = width & 0xFF;
    hdr[13]      = width >> 8;
    hdr[14]      = height & 0xFF;
    hdr[15]      = height >> 8;
    hdr[16]      = 32;
    hdr[17]      = (descriptor & 0x30) | 8; // keep the origin, 8 alpha bits

    uint8_t *dst   = out.data() + 18;
    auto putPixel  = [&](const uint8_t *src) {
        dst[0] = src[0]; // B
        dst[1] = src[1]; // G
        dst[2] = src[2]; // R
        dst[3] = srcBytes == 4 ? src[3] : 0xFF;
        dst += 4;
    };

    uint32_t pos  = offset;
    uint32_t done = 0;
    if (imageType == 2) {
        if (pos + pixelCount * srcBytes > inSize) {
            return false;
        }
        for (; done < pixelCount; done++) {
            putPixel(in + pos);
            pos += srcBytes;
        }
        return true;
    }

    while (done < pixelCount) {
        if (pos >= inSize) {
            return false;
        }
        uint8_t packet = in[pos++];
        uint32_t count = (packet & 0x7F) + 1;
        if (count > pixelCount - done) {
            return false;
        }
        if (packet & 0x80) {
            if (pos + srcBytes > inSize) {
                return false;
            }
            for (uint32_t i = 0; i < count; i++) {
                putPixel(in + pos);
            }
            pos += srcBytes;
        } else {
            if (pos + count * srcBytes > inSize) {
                return false;
            }
            for (uint32_t i = 0; i < count; i++) {
                putPixel(in + pos);
                pos += srcBytes;
            }
        }
        done += count;
    }
    return true;
}
