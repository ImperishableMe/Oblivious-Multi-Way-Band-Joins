#pragma once

#include "../../common/enclave_types.h"
#include "obl_primitives.h"          // ObliviousAssignHelper{,16,32}
#include "obl_building_blocks.h"     // obligraph::o_mem_swap (generic to specialize)

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace obligraph {

// Specialized in-place oblivious conditional swap for entry_t.
//
// entry_t = 13 int32_t metadata + 64 int32_t attributes = 308 bytes.
// 308 = 9 * 32 + 1 * 16 + 1 * 4 (no 2 B / 1 B tail). With AVX2 the loop
// does a single vpblendvb per 32 B chunk, writing back to both halves
// in one pass — no temporary entry_t, no return-by-value copies.
//
// Obliviousness: the loads, blends, and stores are issued in the same
// order at the same addresses regardless of `cond`. The cond bit only
// flows through the per-byte blend mask register.
template <>
inline void o_mem_swap<entry_t>(entry_t &a, entry_t &b, bool cond) {
    auto* pa = reinterpret_cast<char*>(&a);
    auto* pb = reinterpret_cast<char*>(&b);
    size_t off = 0;

#ifdef USE_AVX2
    const __m256i mask256 = _mm256_set1_epi64x((int64_t)cond * -1);
    for (; off + 32 <= sizeof(entry_t); off += 32) {
        __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pa + off));
        __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pb + off));
        // mask=1 selects t_val (here: the *other* element), so vpblendvb(b,a,m=1) = a;
        // we want a' = cond ? b : a, so pass (a, b, mask).
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(pa + off),
                            _mm256_blendv_epi8(va, vb, mask256));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(pb + off),
                            _mm256_blendv_epi8(vb, va, mask256));
    }
    if (off + 16 <= sizeof(entry_t)) {
        const __m128i mask128 = _mm_set1_epi64x((int64_t)cond * -1);
        __m128i va = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pa + off));
        __m128i vb = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pb + off));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(pa + off),
                         _mm_blendv_epi8(va, vb, mask128));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(pb + off),
                         _mm_blendv_epi8(vb, va, mask128));
        off += 16;
    }
    // 4 B tail.
    for (; off + 4 <= sizeof(entry_t); off += 4) {
        uint32_t* qa = reinterpret_cast<uint32_t*>(pa + off);
        uint32_t* qb = reinterpret_cast<uint32_t*>(pb + off);
        uint32_t ta = *qa, tb = *qb;
        obl::ObliviousAssignHelper(cond, tb, ta, qa);
        obl::ObliviousAssignHelper(cond, ta, tb, qb);
    }
#else
    // Scalar fallback: 8-byte CMOV chunks, then 4-byte tail.
    for (; off + 8 <= sizeof(entry_t); off += 8) {
        uint64_t* qa = reinterpret_cast<uint64_t*>(pa + off);
        uint64_t* qb = reinterpret_cast<uint64_t*>(pb + off);
        uint64_t ta = *qa, tb = *qb;
        obl::ObliviousAssignHelper(cond, tb, ta, qa);
        obl::ObliviousAssignHelper(cond, ta, tb, qb);
    }
    for (; off + 4 <= sizeof(entry_t); off += 4) {
        uint32_t* qa = reinterpret_cast<uint32_t*>(pa + off);
        uint32_t* qb = reinterpret_cast<uint32_t*>(pb + off);
        uint32_t ta = *qa, tb = *qb;
        obl::ObliviousAssignHelper(cond, tb, ta, qa);
        obl::ObliviousAssignHelper(cond, ta, tb, qb);
    }
#endif
}

}  // namespace obligraph
