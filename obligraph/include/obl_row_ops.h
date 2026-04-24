#pragma once

// Specialized oblivious operations for Row (exactly 64 B = one AVX-512 register,
// or two AVX2 registers). Replaces the generic byte-loop ObliviousBytesAssign
// path with a tight vector blend for sort / probe inner loops.

#include <immintrin.h>
#include <cstdint>
#include "definitions.h"
#include "obl_building_blocks.h"

namespace obligraph {

// Constant-time mask: 0 or ~0 from bool without a branch.
// Compiler emits `neg` on sign-extended integer; no jump.
static inline int64_t obl_mask64(bool cond) noexcept {
    return -static_cast<int64_t>(cond);
}

// dst = cond ? t_val : f_val, for a full 64-byte Row.
// Unconditional loads + blend + unconditional store: no timing channel on cond.
inline void obl_row_select(Row& dst, const Row& t_val, const Row& f_val,
                           bool cond) noexcept {
#if defined(__AVX512BW__)
    __m512i vt = _mm512_loadu_si512(&t_val);
    __m512i vf = _mm512_loadu_si512(&f_val);
    __mmask64 m = static_cast<__mmask64>(obl_mask64(cond));
    _mm512_storeu_si512(&dst, _mm512_mask_blend_epi8(m, vf, vt));
#else
    __m256i mask = _mm256_set1_epi64x(obl_mask64(cond));
    __m256i t_lo = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&t_val));
    __m256i t_hi = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(
                        reinterpret_cast<const char*>(&t_val) + 32));
    __m256i f_lo = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&f_val));
    __m256i f_hi = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(
                        reinterpret_cast<const char*>(&f_val) + 32));
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(&dst),
                        _mm256_blendv_epi8(f_lo, t_lo, mask));
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(
                            reinterpret_cast<char*>(&dst) + 32),
                        _mm256_blendv_epi8(f_hi, t_hi, mask));
#endif
}

// Oblivious swap of two Rows under cond. 2 loads + 2 blends + 2 stores on
// AVX-512. Versus the generic path's 6+ full-row copies, this is the floor.
inline void obl_row_swap(Row& a, Row& b, bool cond) noexcept {
#if defined(__AVX512BW__)
    __m512i va = _mm512_loadu_si512(&a);
    __m512i vb = _mm512_loadu_si512(&b);
    __mmask64 m = static_cast<__mmask64>(obl_mask64(cond));
    _mm512_storeu_si512(&a, _mm512_mask_blend_epi8(m, va, vb));
    _mm512_storeu_si512(&b, _mm512_mask_blend_epi8(m, vb, va));
#else
    __m256i mask = _mm256_set1_epi64x(obl_mask64(cond));
    __m256i a_lo = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&a));
    __m256i a_hi = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(
                        reinterpret_cast<const char*>(&a) + 32));
    __m256i b_lo = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&b));
    __m256i b_hi = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(
                        reinterpret_cast<const char*>(&b) + 32));
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(&a),
                        _mm256_blendv_epi8(a_lo, b_lo, mask));
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(
                            reinterpret_cast<char*>(&a) + 32),
                        _mm256_blendv_epi8(a_hi, b_hi, mask));
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(&b),
                        _mm256_blendv_epi8(b_lo, a_lo, mask));
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(
                            reinterpret_cast<char*>(&b) + 32),
                        _mm256_blendv_epi8(b_hi, a_hi, mask));
#endif
}

// Row overload of o_compare_and_swap — preferred over the generic template
// when T = Row because it's less templated. Must be visible at the point
// o_merge<Row, ...> is instantiated (include this header before parallel_sort).
template <typename Comparator>
inline void o_compare_and_swap(Row& a, Row& b, Comparator cmp, bool asc = true) {
    bool cond = !(cmp(a, b) == asc);
    obl_row_swap(a, b, cond);
}

}  // namespace obligraph
