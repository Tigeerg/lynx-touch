// sha256.h - SHA-256 (FIPS 180-4) for probes checking against the Python reference.
//
// The probes print the sha256 of the same serializations that hashlib computes in
// Python (the bytes of a numpy array of the given dtype). Qt is not needed here: the
// core and the STEP layer do not depend on Qt, and QCryptographicHash would drag Qt
// into targets without Qt dependencies. The header is shared: there are already four
// check probes, a hash copy in each would be four diverging copies of one algorithm.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace gv {

class Sha256 {
public:
    void update(const unsigned char* data, size_t len) {
        total_ += len;
        for (size_t i = 0; i < len; ++i) {
            block_[filled_++] = data[i];
            if (filled_ == 64) { compress(); }
        }
    }

    std::string hex() {
        const unsigned long long bits = total_ * 8ULL;
        const unsigned char pad = 0x80;
        update(&pad, 1);
        const unsigned char zero = 0x00;
        while (filled_ != 56) { update(&zero, 1); }
        unsigned char tail[8];
        for (int i = 0; i < 8; ++i) { tail[i] = static_cast<unsigned char>(bits >> (56 - 8 * i)); }
        update(tail, 8);
        static const char* digits = "0123456789abcdef";
        std::string out;
        out.reserve(64);
        for (int i = 0; i < 8; ++i) {
            for (int k = 3; k >= 0; --k) {
                const unsigned int byte = static_cast<unsigned int>((state_[i] >> (8 * k)) & 0xffU);
                out.push_back(digits[byte >> 4]);
                out.push_back(digits[byte & 0xf]);
            }
        }
        return out;
    }

private:
    static unsigned int rotr(unsigned int x, int n) { return (x >> n) | (x << (32 - n)); }

    void compress() {
        unsigned int w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<unsigned int>(block_[4 * i]) << 24)
                 | (static_cast<unsigned int>(block_[4 * i + 1]) << 16)
                 | (static_cast<unsigned int>(block_[4 * i + 2]) << 8)
                 | static_cast<unsigned int>(block_[4 * i + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const unsigned int s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const unsigned int s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        unsigned int a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        unsigned int e = state_[4], f = state_[5], g = state_[6], h = state_[7];
        static const unsigned int k[64] = {
            0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U,
            0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
            0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U,
            0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
            0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
            0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
            0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
            0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
            0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU,
            0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
            0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};
        for (int i = 0; i < 64; ++i) {
            const unsigned int s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const unsigned int ch = (e & f) ^ ((~e) & g);
            const unsigned int t1 = h + s1 + ch + k[i] + w[i];
            const unsigned int s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const unsigned int maj = (a & b) ^ (a & c) ^ (b & c);
            const unsigned int t2 = s0 + maj;
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
        state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
        filled_ = 0;
    }

    unsigned int state_[8] = {0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                              0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    unsigned char block_[64] = {};
    size_t filled_ = 0;
    unsigned long long total_ = 0;   // counter over full blocks only: the length is completed by the tail
};

inline std::string sha256Hex(const std::vector<unsigned char>& bytes) {
    Sha256 h;
    h.update(bytes.data(), bytes.size());
    return h.hex();
}

} // namespace gv
