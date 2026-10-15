#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

// Public-domain puff-style zlib inflater (RFC 1950 + RFC 1951).
namespace zlib_inflate {
namespace {

struct BitReader {
    const uint8_t* p = nullptr;
    const uint8_t* end = nullptr;
    uint32_t acc = 0;
    int bits = 0;

    bool Load(int n) {
        while (bits < n) {
            if (p >= end) return false;
            acc |= static_cast<uint32_t>(*p++) << bits;
            bits += 8;
        }
        return true;
    }
    bool Bits(int n, uint32_t& v) {
        if (!Load(n)) return false;
        v = acc & ((1u << n) - 1);
        acc >>= n;
        bits -= n;
        return true;
    }
};

struct Huffman {
    int counts[16]{};
    int symbols[288]{};
    int max = 0;

    bool Build(const int* lengths, int n) {
        std::memset(counts, 0, sizeof(counts));
        max = 0;
        for (int i = 0; i < n; ++i) {
            const int l = lengths[i];
            if (l < 0 || l > 15) return false;
            counts[l]++;
            if (l > max) max = l;
        }
        if (counts[0] == n) return true;
        int left = 1;
        for (int len = 1; len <= 15; ++len) {
            left <<= 1;
            left -= counts[len];
            if (left < 0) return false;
        }
        int offs[16]{};
        for (int len = 1; len < 15; ++len) offs[len + 1] = offs[len] + counts[len];
        for (int i = 0; i < n; ++i) {
            if (lengths[i]) symbols[offs[lengths[i]]++] = i;
        }
        return true;
    }

    bool Decode(BitReader& br, int& sym) const {
        int code = 0;
        int first = 0;
        int index = 0;
        for (int len = 1; len <= 15; ++len) {
            uint32_t bit = 0;
            if (!br.Bits(1, bit)) return false;
            code |= static_cast<int>(bit);
            const int count = counts[len];
            if (code - first < count) {
                sym = symbols[index + (code - first)];
                return true;
            }
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        return false;
    }
};

constexpr int kLenBase[] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
constexpr int kLenExtra[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
constexpr int kDistBase[] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};
constexpr int kDistExtra[] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};
constexpr int kOrder[] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

inline bool Codes(BitReader& br, std::vector<uint8_t>& out, const Huffman& lit, const Huffman& dist) {
    for (;;) {
        int sym = 0;
        if (!lit.Decode(br, sym)) return false;
        if (sym < 256) {
            out.push_back(static_cast<uint8_t>(sym));
            continue;
        }
        if (sym == 256) return true;
        if (sym > 285) return false;
        const int lenIdx = sym - 257;
        uint32_t extra = 0;
        if (kLenExtra[lenIdx] && !br.Bits(kLenExtra[lenIdx], extra)) return false;
        const int length = kLenBase[lenIdx] + static_cast<int>(extra);
        int dsym = 0;
        if (!dist.Decode(br, dsym) || dsym > 29) return false;
        extra = 0;
        if (kDistExtra[dsym] && !br.Bits(kDistExtra[dsym], extra)) return false;
        const int distance = kDistBase[dsym] + static_cast<int>(extra);
        if (distance <= 0 || distance > static_cast<int>(out.size())) return false;
        for (int i = 0; i < length; ++i) {
            out.push_back(out[out.size() - distance]);
        }
    }
}

inline bool FixedBlock(BitReader& br, std::vector<uint8_t>& out) {
    int lengths[288];
    for (int i = 0; i < 144; ++i) lengths[i] = 8;
    for (int i = 144; i < 256; ++i) lengths[i] = 9;
    for (int i = 256; i < 280; ++i) lengths[i] = 7;
    for (int i = 280; i < 288; ++i) lengths[i] = 8;
    Huffman lit, dist;
    if (!lit.Build(lengths, 288)) return false;
    int dlen[32];
    for (int i = 0; i < 32; ++i) dlen[i] = 5;
    if (!dist.Build(dlen, 32)) return false;
    return Codes(br, out, lit, dist);
}

inline bool DynamicBlock(BitReader& br, std::vector<uint8_t>& out) {
    uint32_t hlit = 0, hdist = 0, hclen = 0;
    if (!br.Bits(5, hlit) || !br.Bits(5, hdist) || !br.Bits(4, hclen)) return false;
    const int nlit = static_cast<int>(hlit) + 257;
    const int ndist = static_cast<int>(hdist) + 1;
    const int nclen = static_cast<int>(hclen) + 4;
    if (nlit > 286 || ndist > 32) return false;
    int clen[19]{};
    for (int i = 0; i < nclen; ++i) {
        uint32_t v = 0;
        if (!br.Bits(3, v)) return false;
        clen[kOrder[i]] = static_cast<int>(v);
    }
    Huffman cl;
    if (!cl.Build(clen, 19)) return false;
    int lengths[318]{};
    int n = 0;
    const int total = nlit + ndist;
    while (n < total) {
        int sym = 0;
        if (!cl.Decode(br, sym)) return false;
        if (sym < 16) {
            lengths[n++] = sym;
        } else {
            int rep = 0, val = 0;
            if (sym == 16) {
                if (n == 0) return false;
                uint32_t e = 0;
                if (!br.Bits(2, e)) return false;
                rep = 3 + static_cast<int>(e);
                val = lengths[n - 1];
            } else if (sym == 17) {
                uint32_t e = 0;
                if (!br.Bits(3, e)) return false;
                rep = 3 + static_cast<int>(e);
            } else if (sym == 18) {
                uint32_t e = 0;
                if (!br.Bits(7, e)) return false;
                rep = 11 + static_cast<int>(e);
            } else {
                return false;
            }
            if (n + rep > total) return false;
            while (rep--) lengths[n++] = val;
        }
    }
    Huffman lit, dist;
    if (!lit.Build(lengths, nlit)) return false;
    if (!dist.Build(lengths + nlit, ndist)) return false;
    return Codes(br, out, lit, dist);
}

inline bool StoredBlock(BitReader& br, std::vector<uint8_t>& out) {
    br.acc = 0;
    br.bits = 0;
    if (br.p + 4 > br.end) return false;
    const uint16_t len = static_cast<uint16_t>(br.p[0] | (br.p[1] << 8));
    const uint16_t nlen = static_cast<uint16_t>(br.p[2] | (br.p[3] << 8));
    br.p += 4;
    if (static_cast<uint16_t>(len ^ 0xFFFF) != nlen) return false;
    if (br.p + len > br.end) return false;
    out.insert(out.end(), br.p, br.p + len);
    br.p += len;
    return true;
}

}  // namespace

inline bool InflateZlib(const uint8_t* data, size_t size, std::vector<uint8_t>& out) {
    if (!data || size < 2) return false;
    const uint8_t cmf = data[0];
    const uint8_t flg = data[1];
    if ((cmf & 0x0F) != 8) return false;
    if (((cmf << 8) + flg) % 31 != 0) return false;
    if (flg & 0x20) return false;  // dictionary not supported
    BitReader br;
    br.p = data + 2;
    br.end = data + size;
    if (size >= 6) br.end = data + size - 4;  // drop Adler-32
    out.clear();
    out.reserve(size * 4);
    for (;;) {
        uint32_t last = 0, type = 0;
        if (!br.Bits(1, last) || !br.Bits(2, type)) return false;
        bool ok = false;
        if (type == 0) ok = StoredBlock(br, out);
        else if (type == 1) ok = FixedBlock(br, out);
        else if (type == 2) ok = DynamicBlock(br, out);
        if (!ok) return false;
        if (last) return true;
    }
}

}  // namespace zlib_inflate
