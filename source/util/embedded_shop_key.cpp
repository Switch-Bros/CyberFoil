#include "util/embedded_shop_key.hpp"

#include <cstddef>
#include <cstring>
#include <string>
#include <vector>
#include <switch.h>

namespace {
#if defined(EMBEDDED_SHOP_KEY_OBF_HEX) && defined(EMBEDDED_SHOP_KEY_OBF_KEY_HEX)
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

    bool DecodeHexToBytes(const char* hex, std::vector<unsigned char>& out)
    {
        out.clear();
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
            out.push_back(static_cast<unsigned char>((hi << 4) | lo));
        }

        return !out.empty();
    }

    bool Pkcs7UnpadInPlace(std::vector<unsigned char>& data)
    {
        if (data.empty())
            return false;

        const unsigned char padLen = data.back();
        if (padLen == 0 || padLen > AES_BLOCK_SIZE || padLen > data.size())
            return false;

        for (std::size_t i = data.size() - padLen; i < data.size(); i++) {
            if (data[i] != padLen)
                return false;
        }

        data.resize(data.size() - padLen);
        return !data.empty();
    }
#endif
}

namespace inst::util {
    bool LoadEmbeddedShopPrivateKey(std::vector<unsigned char>& out)
    {
        out.clear();

#if defined(EMBEDDED_SHOP_KEY_OBF_HEX) && defined(EMBEDDED_SHOP_KEY_OBF_KEY_HEX)
        std::vector<unsigned char> ciphertext;
        std::vector<unsigned char> key;
        if (!DecodeHexToBytes(EMBEDDED_SHOP_KEY_OBF_HEX, ciphertext))
            return false;
        if (!DecodeHexToBytes(EMBEDDED_SHOP_KEY_OBF_KEY_HEX, key))
            return false;
        if (key.size() != AES_128_KEY_SIZE)
            return false;
        if ((ciphertext.size() % AES_BLOCK_SIZE) != 0)
            return false;

        Aes128Context aesCtx = {};
        aes128ContextCreate(&aesCtx, key.data(), false);

        out.resize(ciphertext.size());
        for (std::size_t offset = 0; offset < ciphertext.size(); offset += AES_BLOCK_SIZE) {
            aes128DecryptBlock(
                &aesCtx,
                out.data() + offset,
                ciphertext.data() + offset
            );
        }

        return Pkcs7UnpadInPlace(out);
#else
        return false;
#endif
    }
}
