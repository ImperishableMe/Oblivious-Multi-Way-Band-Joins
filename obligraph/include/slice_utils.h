#pragma once
#include <cstddef>
#include <vector>

namespace obligraph {

struct Slice { size_t begin; size_t end; };

inline std::vector<Slice> buildSlices(size_t n, size_t P) {
    std::vector<Slice> out;
    if (P == 0) P = 1;
    if (n == 0) return out;
    out.reserve(P);
    size_t q = n / P, r = n % P, off = 0;
    for (size_t t = 0; t < P; ++t) {
        size_t sz = q + (t < r ? 1 : 0);
        if (sz == 0) break;
        out.push_back({off, off + sz});
        off += sz;
    }
    return out;
}

} // namespace obligraph
