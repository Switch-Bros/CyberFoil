#include "util/hauth.hpp"

#include <array>
#include <cctype>
#include <cstring>
#include <algorithm>
#include <string>

#include <mbedtls/aes.h>
#include <switch.h>

namespace {
    constexpr unsigned char kSeedXorBase = 0xA7;

    unsigned char SeedMaskForIndex(std::size_t index)
    {
        return static_cast<unsigned char>(kSeedXorBase + ((index * 37) + 0x5C));
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

    bool DecodeObfuscatedSeed(std::string& outSeed)
    {
        outSeed.clear();
        const char* hex = nullptr;
        #ifdef HAUTH_SEED_OBF_HEX
        hex = HAUTH_SEED_OBF_HEX;
        #endif
        if (hex == nullptr || hex[0] == '\0')
            return false;

        const std::size_t hexLen = std::strlen(hex);
        if ((hexLen % 2) != 0)
            return false;

        outSeed.reserve(hexLen / 2);
        for (std::size_t i = 0; i < hexLen; i += 2) {
            const int hi = HexNibble(hex[i]);
            const int lo = HexNibble(hex[i + 1]);
            if (hi < 0 || lo < 0) {
                outSeed.clear();
                return false;
            }

            const std::size_t byteIndex = i / 2;
            const unsigned char obf = static_cast<unsigned char>((hi << 4) | lo);
            const unsigned char plain = static_cast<unsigned char>(obf ^ SeedMaskForIndex(byteIndex));
            outSeed.push_back(static_cast<char>(plain));
        }

        return !outSeed.empty();
    }

    void TrimAsciiInPlace(std::string& s)
    {
        const auto start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) {
            s.clear();
            return;
        }
        const auto end = s.find_last_not_of(" \t\r\n");
        s = s.substr(start, (end - start) + 1);
    }

    void XorBlock(std::array<unsigned char, 16>& out, const std::array<unsigned char, 16>& a, const std::array<unsigned char, 16>& b)
    {
        for (std::size_t i = 0; i < out.size(); i++)
            out[i] = static_cast<unsigned char>(a[i] ^ b[i]);
    }

    void LeftShiftOne(std::array<unsigned char, 16>& block)
    {
        unsigned char carry = 0;
        for (int i = static_cast<int>(block.size()) - 1; i >= 0; i--) {
            const unsigned char nextCarry = static_cast<unsigned char>((block[static_cast<std::size_t>(i)] & 0x80) ? 1 : 0);
            block[static_cast<std::size_t>(i)] = static_cast<unsigned char>((block[static_cast<std::size_t>(i)] << 1) | carry);
            carry = nextCarry;
        }
    }

    std::array<unsigned char, 16> DeriveSubkey(const std::array<unsigned char, 16>& in)
    {
        std::array<unsigned char, 16> out = in;
        const bool msbSet = (out[0] & 0x80) != 0;
        LeftShiftOne(out);
        if (msbSet)
            out[15] ^= 0x87;
        return out;
    }

    bool TryBuildHostPart(const std::string& url, std::string& outHostPart)
    {
        outHostPart.clear();
        if (url.empty())
            return false;

        const std::size_t schemeSep = url.find("://");
        if (schemeSep == std::string::npos || schemeSep == 0)
            return false;

        const std::size_t netlocStart = schemeSep + 3;
        if (netlocStart >= url.size())
            return false;

        const std::size_t netlocEnd = url.find_first_of("/?#", netlocStart);
        const std::size_t netlocLen = (netlocEnd == std::string::npos) ? (url.size() - netlocStart) : (netlocEnd - netlocStart);
        if (netlocLen == 0)
            return false;

        std::string scheme = url.substr(0, schemeSep);
        for (char& c : scheme) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        std::string netloc = url.substr(netlocStart, netlocLen);
        if (scheme == "https") {
            if (netloc.size() > 4 && netloc.rfind(":443") == netloc.size() - 4)
                netloc.resize(netloc.size() - 4);
        } else if (scheme == "http") {
            if (netloc.size() > 3 && netloc.rfind(":80") == netloc.size() - 3)
                netloc.resize(netloc.size() - 3);
        }

        outHostPart = scheme + "://" + netloc;
        return true;
    }

    void EncryptBlockAes128(const std::array<unsigned char, 16>& key, const std::array<unsigned char, 16>& in, std::array<unsigned char, 16>& out)
    {
        mbedtls_aes_context ctx;
        mbedtls_aes_init(&ctx);
        mbedtls_aes_setkey_enc(&ctx, key.data(), 128);
        mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, in.data(), out.data());
        mbedtls_aes_free(&ctx);
    }

    std::array<unsigned char, 16> ComputeAesCmac(const std::array<unsigned char, 16>& key, const unsigned char* msg, std::size_t msgLen)
    {
        std::array<unsigned char, 16> zero = {};
        std::array<unsigned char, 16> l = {};
        EncryptBlockAes128(key, zero, l);

        const std::array<unsigned char, 16> k1 = DeriveSubkey(l);
        const std::array<unsigned char, 16> k2 = DeriveSubkey(k1);

        const std::size_t blockSize = 16;
        std::size_t n = (msgLen + blockSize - 1) / blockSize;
        if (n == 0)
            n = 1;

        const bool isCompleteLastBlock = (msgLen != 0) && ((msgLen % blockSize) == 0);
        std::array<unsigned char, 16> mLast = {};

        if (isCompleteLastBlock) {
            const unsigned char* last = msg + ((n - 1) * blockSize);
            for (std::size_t i = 0; i < blockSize; i++)
                mLast[i] = static_cast<unsigned char>(last[i] ^ k1[i]);
        } else {
            const std::size_t rem = msgLen % blockSize;
            const unsigned char* last = (msgLen >= rem) ? (msg + (msgLen - rem)) : msg;
            for (std::size_t i = 0; i < rem; i++)
                mLast[i] = last[i];
            mLast[rem] = 0x80;
            for (std::size_t i = 0; i < blockSize; i++)
                mLast[i] ^= k2[i];
        }

        std::array<unsigned char, 16> x = {};
        std::array<unsigned char, 16> y = {};
        std::array<unsigned char, 16> m = {};

        for (std::size_t i = 0; i + 1 < n; i++) {
            const unsigned char* block = msg + (i * blockSize);
            for (std::size_t j = 0; j < blockSize; j++)
                m[j] = block[j];
            XorBlock(y, x, m);
            EncryptBlockAes128(key, y, x);
        }

        XorBlock(y, x, mLast);
        std::array<unsigned char, 16> tag = {};
        EncryptBlockAes128(key, y, tag);
        return tag;
    }
}

namespace inst::util {
    std::string ComputeHauthFromUrl(const std::string& requestUrl)
    {
        std::string hostPart;
        if (!TryBuildHostPart(requestUrl, hostPart))
            return "0";

        std::string seed;
        if (!DecodeObfuscatedSeed(seed))
            return "0";
        TrimAsciiInPlace(seed);
        if (seed.empty())
            return "0";

        std::array<unsigned char, 32> seedHash = {};
        sha256CalculateHash(seedHash.data(), seed.data(), seed.size());

        std::array<unsigned char, 16> key = {};
        for (std::size_t i = 0; i < key.size(); i++)
            key[i] = seedHash[i];

        std::string msg = hostPart;
        msg.push_back('\0');
        const std::array<unsigned char, 16> mac = ComputeAesCmac(
            key,
            reinterpret_cast<const unsigned char*>(msg.data()),
            msg.size());

        static constexpr char kHex[] = "0123456789ABCDEF";
        std::string hex;
        hex.reserve(mac.size() * 2);
        for (unsigned char b : mac) {
            hex.push_back(kHex[(b >> 4) & 0xF]);
            hex.push_back(kHex[b & 0xF]);
        }

        std::fill(seed.begin(), seed.end(), '\0');
        std::fill(seedHash.begin(), seedHash.end(), 0);
        std::fill(key.begin(), key.end(), 0);

        return hex;
    }
}
