/*
 * Copyright (c) 2010-2024 OTClient <https://github.com/edubart/otclient>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "crypt.h"
#include "framework/core/application.h"
#include <framework/core/logger.h>
#include <framework/core/resourcemanager.h>
#include <framework/platform/platform.h>
#include <framework/stdext/math.h>

#ifndef USE_GMP
#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/rsa.h>
#endif
#include <zlib.h>

#include <algorithm>

#include "framework/core/graphicalapplication.h"

static constexpr std::string_view base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static inline bool is_base64(const uint8_t c) { return (isalnum(c) || (c == '+') || (c == '/')); }

Crypt g_crypt;

Crypt::Crypt()
{
#ifdef USE_GMP
    mpz_init(m_p);
    mpz_init(m_q);
    mpz_init(m_d);
    mpz_init(m_e);
    mpz_init(m_n);
#else
    m_rsa = RSA_new();
#endif
}

Crypt::~Crypt()
{
#ifdef USE_GMP
    mpz_clear(m_p);
    mpz_clear(m_q);
    mpz_clear(m_n);
    mpz_clear(m_d);
    mpz_clear(m_e);
#else
    RSA_free(m_rsa);
#endif
}

std::string Crypt::base64Encode(const std::string& decoded_string)
{
    std::string ret;
    int i = 0;
    uint8_t char_array_3[3];
    uint8_t char_array_4[4];
    int pos = 0;
    int len = decoded_string.size();

    while (len--) {
        char_array_3[i++] = decoded_string[pos++];
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; (i < 4); i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        int j = 0;
        for (j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (j = 0; (j < i + 1); j++)
            ret += base64_chars[char_array_4[j]];

        while ((i++ < 3))
            ret += '=';
    }

    return ret;
}

std::string Crypt::base64Decode(const std::string_view& encoded_string)
{
    int len = encoded_string.size();
    int i = 0;
    int in_ = 0;
    uint8_t char_array_4[4], char_array_3[3];
    std::string ret;

    while (len-- && (encoded_string[in_] != '=') && is_base64(encoded_string[in_])) {
        char_array_4[i++] = encoded_string[in_]; in_++;
        if (i == 4) {
            for (i = 0; i < 4; i++)
                char_array_4[i] = base64_chars.find(char_array_4[i]);

            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

            for (i = 0; (i < 3); i++)
                ret += char_array_3[i];
            i = 0;
        }
    }

    if (i) {
        int j = 0;
        for (j = i; j < 4; j++)
            char_array_4[j] = 0;

        for (j = 0; j < 4; j++)
            char_array_4[j] = base64_chars.find(char_array_4[j]);

        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

        for (j = 0; (j < i - 1); j++)
            ret += char_array_3[j];
    }

    return ret;
}

std::string Crypt::xorCrypt(const std::string& buffer, const std::string& key)
{
    std::string out;
    out.resize(buffer.size());
    size_t j = 0;
    for (size_t i = 0; i < buffer.size(); ++i) {
        out[i] = buffer[i] ^ key[j++];
        if (j >= key.size())
            j = 0;
    }
    return out;
}

std::string Crypt::genUUID()
{
    std::random_device rd;
    auto seed_data = std::array<int, std::mt19937::state_size>{};
    std::ranges::generate(seed_data, std::ref(rd));
    std::seed_seq seq(std::begin(seed_data), std::end(seed_data));
    std::mt19937 generator(seq);

    return to_string(uuids::uuid_random_generator{ generator }());
}

bool Crypt::setMachineUUID(std::string uuidstr)
{
    if (uuidstr.empty())
        return false;

    uuidstr = _decrypt(uuidstr, false);

    if (uuidstr.length() != 36)
        return false;

    m_machineUUID = uuids::uuid::from_string(uuidstr).value();

    return true;
}

std::string Crypt::getMachineUUID()
{
    if (m_machineUUID.is_nil()) {
        std::random_device rd;
        auto seed_data = std::array<int, std::mt19937::state_size>{};
        std::ranges::generate(seed_data, std::ref(rd));
        std::seed_seq seq(std::begin(seed_data), std::end(seed_data));
        std::mt19937 generator(seq);

        m_machineUUID = uuids::uuid_random_generator{ generator }();
    }
    return _encrypt(to_string(m_machineUUID), false);
}

std::string Crypt::getCryptKey(const bool useMachineUUID) const
{
    constexpr std::hash<uuids::uuid> uuid_hasher;
    const uuids::uuid uuid = useMachineUUID ? m_machineUUID : uuids::uuid();
    const uuids::uuid u = uuids::uuid_name_generator(uuid)
        (g_app.getCompactName() + g_platform.getCPUName() + g_platform.getOSName() + g_resources.getUserDir());
    const std::size_t hash = uuid_hasher(u);

    std::string key;
    key.assign((const char*)&hash, sizeof(hash));
    return key;
}

std::string Crypt::_encrypt(const std::string& decrypted_string, const bool useMachineUUID)
{
    const uint32_t sum = stdext::adler32((const uint8_t*)decrypted_string.c_str(), decrypted_string.size());

    std::string tmp = "0000" + decrypted_string;

    stdext::writeULE32((uint8_t*)&tmp[0], sum);
    std::string encrypted = base64Encode(xorCrypt(tmp, getCryptKey(useMachineUUID)));
    return encrypted;
}

std::string Crypt::_decrypt(const std::string& encrypted_string, const bool useMachineUUID)
{
    const auto& decoded = base64Decode(encrypted_string);
    const auto& tmp = xorCrypt(decoded, getCryptKey(useMachineUUID));

    if (tmp.length() >= 4) {
        const uint32_t readsum = stdext::readULE32((const uint8_t*)tmp.c_str());
        std::string decrypted_string = tmp.substr(4);
        const uint32_t sum = stdext::adler32((const uint8_t*)decrypted_string.c_str(), decrypted_string.size());
        if (readsum == sum)
            return decrypted_string;
    }
    return {};
}

void Crypt::rsaSetPublicKey(const std::string& n, const std::string& e)
{
#ifdef USE_GMP
    mpz_set_str(m_n, n, 10);
    mpz_set_str(m_e, e, 10);
#else
#if OPENSSL_VERSION_NUMBER < 0x10100005L
    BN_dec2bn(&m_rsa->n, n);
    BN_dec2bn(&m_rsa->e, e);
    // clear rsa cache
    if (m_rsa->_method_mod_n) {
        BN_MONT_CTX_free(m_rsa->_method_mod_n);
        m_rsa->_method_mod_n = nullptr;
    }
#else
    BIGNUM* bn = nullptr, * be = nullptr;
    BN_dec2bn(&bn, n.c_str());
    BN_dec2bn(&be, e.c_str());
    RSA_set0_key(m_rsa, bn, be, nullptr);
#endif
#endif
}

void Crypt::rsaSetPrivateKey(const std::string& p, const std::string& q, const std::string& d)
{
#ifdef USE_GMP
    mpz_set_str(m_p, p, 10);
    mpz_set_str(m_q, q, 10);
    mpz_set_str(m_d, d, 10);

    // n = p * q
    mpz_mul(m_n, m_p, m_q);
#else
#if OPENSSL_VERSION_NUMBER < 0x10100005L
    BN_dec2bn(&m_rsa->p, p);
    BN_dec2bn(&m_rsa->q, q);
    BN_dec2bn(&m_rsa->d, d);
    // clear rsa cache
    if (m_rsa->_method_mod_p) {
        BN_MONT_CTX_free(m_rsa->_method_mod_p);
        m_rsa->_method_mod_p = nullptr;
    }
    if (m_rsa->_method_mod_q) {
        BN_MONT_CTX_free(m_rsa->_method_mod_q);
        m_rsa->_method_mod_q = nullptr;
    }
#else
    BIGNUM* bp = nullptr, * bq = nullptr, * bd = nullptr;
    BN_dec2bn(&bp, p.c_str());
    BN_dec2bn(&bq, q.c_str());
    BN_dec2bn(&bd, d.c_str());
    RSA_set0_key(m_rsa, nullptr, nullptr, bd);
    RSA_set0_factors(m_rsa, bp, bq);
#endif
#endif
}

bool Crypt::rsaEncrypt(uint8_t* msg, int size)
{
    if (size != rsaGetSize())
        return false;

#ifdef USE_GMP
    mpz_t c, m;
    mpz_init(c);
    mpz_init(m);
    mpz_import(m, size, 1, 1, 0, 0, msg);

    // c = m^e mod n
    mpz_powm(c, m, m_e, m_n);

    size_t count = (mpz_sizeinbase(m, 2) + 7) / 8;
    memset((char*)msg, 0, size - count);
    mpz_export((char*)msg + (size - count), nullptr, 1, 1, 0, 0, c);

    mpz_clear(c);
    mpz_clear(m);

    return true;
#else
    return RSA_public_encrypt(size, msg, msg, m_rsa, RSA_NO_PADDING) != -1;
#endif
}

bool Crypt::rsaDecrypt(uint8_t* msg, int size)
{
    if (size != rsaGetSize())
        return false;

#ifdef USE_GMP
    mpz_t c, m;
    mpz_init(c);
    mpz_init(m);
    mpz_import(c, size, 1, 1, 0, 0, msg);

    // m = c^d mod n
    mpz_powm(m, c, m_d, m_n);

    size_t count = (mpz_sizeinbase(m, 2) + 7) / 8;
    memset((char*)msg, 0, size - count);
    mpz_export((char*)msg + (size - count), nullptr, 1, 1, 0, 0, m);

    mpz_clear(c);
    mpz_clear(m);

    return true;
#else
    return RSA_private_decrypt(size, msg, msg, m_rsa, RSA_NO_PADDING) != -1;
#endif
}

int Crypt::rsaGetSize()
{
#ifdef USE_GMP
    size_t count = (mpz_sizeinbase(m_n, 2) + 7) / 8;
    return ((int)count / 128) * 128;
#else
    return RSA_size(m_rsa);
#endif
}

std::string Crypt::crc32(const std::string& decoded_string, const bool upperCase)
{
    uint32_t crc = ::crc32(0, nullptr, 0);
    crc = ::crc32(crc, (const Bytef*)decoded_string.c_str(), decoded_string.size());
    std::string result = stdext::dec_to_hex(crc);
    if (upperCase)
        std::ranges::transform(result, result.begin(), toupper);
    else
        std::ranges::transform(result, result.begin(), tolower);
    return result;
}

std::string Crypt::sha1Encrpyt(const std::string& input)
{
    uint32_t H[] = {
        0x67452301,
        0xEFCDAB89,
        0x98BADCFE,
        0x10325476,
        0xC3D2E1F0
    };

    uint8_t messageBlock[64];
    size_t index = 0;

    uint32_t length_low = 0;
    uint32_t length_high = 0;
    for (const char ch : input) {
        messageBlock[index++] = ch;

        length_low += 8;
        if (length_low == 0) {
            length_high++;
        }

        if (index == 64) {
            sha1Block(messageBlock, H);
            index = 0;
        }
    }

    messageBlock[index++] = 0x80;

    if (index > 56) {
        while (index < 64) {
            messageBlock[index++] = 0;
        }

        sha1Block(messageBlock, H);
        index = 0;
    }

    while (index < 56) {
        messageBlock[index++] = 0;
    }

    messageBlock[56] = length_high >> 24;
    messageBlock[57] = length_high >> 16;
    messageBlock[58] = length_high >> 8;
    messageBlock[59] = length_high;

    messageBlock[60] = length_low >> 24;
    messageBlock[61] = length_low >> 16;
    messageBlock[62] = length_low >> 8;
    messageBlock[63] = length_low;

    sha1Block(messageBlock, H);

    char hexstring[41];
    static constexpr char hexDigits[] = { "0123456789abcdef" };
    for (int hashByte = 20; --hashByte >= 0;) {
        const uint8_t byte = H[hashByte >> 2] >> (((3 - hashByte) & 3) << 3);
        index = hashByte << 1;
        hexstring[index] = hexDigits[byte >> 4];
        hexstring[index + 1] = hexDigits[byte & 15];
    }
    return std::string(hexstring, 40);
}

void Crypt::sha1Block(const uint8_t* block, uint32_t* H)
{
    uint32_t W[80];
    for (int i = 0; i < 16; ++i) {
        const size_t offset = i << 2;
        W[i] = block[offset] << 24 | block[offset + 1] << 16 | block[offset + 2] << 8 | block[offset + 3];
    }

    for (int i = 16; i < 80; ++i) {
        W[i] = stdext::circularShift(1, W[i - 3] ^ W[i - 8] ^ W[i - 14] ^ W[i - 16]);
    }

    uint32_t A = H[0], B = H[1], C = H[2], D = H[3], E = H[4];

    for (int i = 0; i < 20; ++i) {
        const uint32_t tmp = stdext::circularShift(5, A) + ((B & C) | ((~B) & D)) + E + W[i] + 0x5A827999;
        E = D; D = C; C = stdext::circularShift(30, B); B = A; A = tmp;
    }

    for (int i = 20; i < 40; ++i) {
        const uint32_t tmp = stdext::circularShift(5, A) + (B ^ C ^ D) + E + W[i] + 0x6ED9EBA1;
        E = D; D = C; C = stdext::circularShift(30, B); B = A; A = tmp;
    }

    for (int i = 40; i < 60; ++i) {
        const uint32_t tmp = stdext::circularShift(5, A) + ((B & C) | (B & D) | (C & D)) + E + W[i] + 0x8F1BBCDC;
        E = D; D = C; C = stdext::circularShift(30, B); B = A; A = tmp;
    }

    for (int i = 60; i < 80; ++i) {
        const uint32_t tmp = stdext::circularShift(5, A) + (B ^ C ^ D) + E + W[i] + 0xCA62C1D6;
        E = D; D = C; C = stdext::circularShift(30, B); B = A; A = tmp;
    }

    H[0] += A;
    H[1] += B;
    H[2] += C;
    H[3] += D;
    H[4] += E;
}