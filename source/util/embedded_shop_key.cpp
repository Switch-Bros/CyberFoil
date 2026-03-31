#include "util/embedded_shop_key.hpp"

#include <cstddef>
#include <cstring>
#include <string>

namespace {
    constexpr unsigned char EmbeddedKeyMask(std::size_t index)
    {
        return static_cast<unsigned char>((0x5A + ((index * 29) % 199)) & 0xFF);
    }

    int HexNibble(char c)
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F')
            return 10 + (c - 'A');
        return -1;
    }
}

namespace inst::util {
    bool LoadEmbeddedShopPrivateKey(std::vector<unsigned char>& out)
    {
        out.clear();

        const char* hex = nullptr;
#ifdef EMBEDDED_SHOP_KEY_OBF_HEX
        hex = EMBEDDED_SHOP_KEY_OBF_HEX;
#endif
        if (hex == nullptr || hex[0] == '\0')
            return false;

        const std::size_t hexLen = std::strlen(hex);
        if ((hexLen % 2) != 0)
            return false;

        out.reserve(hexLen / 2);
        for (std::size_t i = 0; i < hexLen; i += 2) {
            const int hi = HexNibble(hex[i]);
            const int lo = HexNibble(hex[i + 1]);
            if (hi < 0 || lo < 0) {
                out.clear();
                return false;
            }

            const std::size_t byteIndex = i / 2;
            const unsigned char obf = static_cast<unsigned char>((hi << 4) | lo);
            out.push_back(static_cast<unsigned char>(obf ^ EmbeddedKeyMask(byteIndex)));
        }

        return !out.empty();
    }
}
