/**
 * Reed-Solomon Erasure Code AVX2 Optimized
 * 
 * This is a simple implementation of Reed-Solomon erasure coding in GF(256) using AVX2 instructions.
 * 
 * Goals:
 * - Simple, easy to understand code
 * - Correctness
 * - Get performance fast enough that we don't need to rely on the systematic code path.  This means we need to be able to encode and decode at least 1 GB/s on a single core.
 * - Use AVX2 for performance, code lifted from rust library.
 * 
 * LICENSE: MIT
 */
#include "rs.h"
#include <immintrin.h>



// Assuming you have these tables precomputed

// AVX2 optimized version of mul1: dst = src * c (c is a single byte) in GF(256)
// Note that it's faster to pad the table to 64 bytes and ignore the tail than the clean up loop
void mul1_avx2(gf *dst, const gf *src, gf c, int sz) {
    const gf* lut = &gf_mul_table[c << 8];
    int i;
    __m256i t0_lo = _mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i*)&lut[0]));
    __m256i t1_lo = _mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i*)&lut[16]));
    __m256i t2_lo = _mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i*)&lut[32]));
    __m256i t3_lo = _mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i*)&lut[48]));

    __m256i t0_hi = _mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i*)&lut[64]));
    __m256i t1_hi = _mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i*)&lut[80]));
    __m256i t2_hi = _mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i*)&lut[96]));
    __m256i t3_hi = _mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i*)&lut[112]));
    __m256i clr_mask = _mm256_set1_epi8(0x0f);
    for (i = 0; i + 64 <= sz; i += 64) {
        __m256i src_lo = _mm256_loadu_si256((__m256i*)&src[i]);
        __m256i src_hi = _mm256_loadu_si256((__m256i*)&src[i + 32]);
        
        __m256i data_0 = _mm256_and_si256(src_lo, clr_mask);
        __m256i dst_lo_0 = _mm256_shuffle_epi8(t0_lo, data_0);
        __m256i dst_hi_0 = _mm256_shuffle_epi8(t0_hi, data_0);

        __m256i data_1 = _mm256_and_si256(_mm256_srli_epi64(src_lo, 4), clr_mask);
        __m256i dst_lo_1 = _mm256_shuffle_epi8(t1_lo, data_1);
        __m256i dst_hi_1 = _mm256_shuffle_epi8(t1_hi, data_1);

        data_0 = _mm256_and_si256(src_hi, clr_mask);
        dst_lo_0 = _mm256_xor_si256(dst_lo_0, _mm256_shuffle_epi8(t2_lo, data_0));
        dst_hi_0 = _mm256_xor_si256(dst_hi_0, _mm256_shuffle_epi8(t2_hi, data_0));

        data_1 = _mm256_and_si256(_mm256_srli_epi64(src_hi, 4), clr_mask);
        dst_lo_1 = _mm256_xor_si256(dst_lo_1, _mm256_shuffle_epi8(t3_lo, data_1));
        dst_hi_1 = _mm256_xor_si256(dst_hi_1, _mm256_shuffle_epi8(t3_hi, data_1));
    
        _mm256_storeu_si256((__m256i*)&dst[i], _mm256_xor_si256(dst_lo_0, dst_lo_1));
        _mm256_storeu_si256((__m256i*)&dst[i + 32], _mm256_xor_si256(dst_hi_0, dst_hi_1));
    }
    
    // Handle remaining elements
    for (; i < sz; i++) {
        dst[i] = gf_mul_table[(c << 8) + src[i]];
    }
}

// AVX2 optimized version of dst += src * c (c is a single byte) in GF(256)
void mul_add1_avx2(gf *dst, const gf *src, gf c, int sz) {
    const gf* lut = &gf_mul_table[c << 8];
    int i;
    __m256i t0_lo = _mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i*)&lut[0]));
    __m256i t1_lo = _mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i*)&lut[16]));
    __m256i t2_lo = _mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i*)&lut[32]));
    __m256i t3_lo = _mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i*)&lut[48]));

    __m256i t0_hi = _mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i*)&lut[64]));
    __m256i t1_hi = _mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i*)&lut[80]));
    __m256i t2_hi = _mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i*)&lut[96]));
    __m256i t3_hi = _mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i*)&lut[112]));
    __m256i clr_mask = _mm256_set1_epi8(0x0f);
    for (i = 0; i + 64 <= sz; i += 64) {
        __m256i src_lo = _mm256_loadu_si256((__m256i*)&src[i]);
        __m256i src_hi = _mm256_loadu_si256((__m256i*)&src[i + 32]);
        
        __m256i data_0 = _mm256_and_si256(src_lo, clr_mask);
        __m256i dst_lo_0 = _mm256_shuffle_epi8(t0_lo, data_0);
        __m256i dst_hi_0 = _mm256_shuffle_epi8(t0_hi, data_0);

        __m256i data_1 = _mm256_and_si256(_mm256_srli_epi64(src_lo, 4), clr_mask);
        __m256i dst_lo_1 = _mm256_shuffle_epi8(t1_lo, data_1);
        __m256i dst_hi_1 = _mm256_shuffle_epi8(t1_hi, data_1);

        data_0 = _mm256_and_si256(src_hi, clr_mask);
        dst_lo_0 = _mm256_xor_si256(dst_lo_0, _mm256_shuffle_epi8(t2_lo, data_0));
        dst_hi_0 = _mm256_xor_si256(dst_hi_0, _mm256_shuffle_epi8(t2_hi, data_0));

        data_1 = _mm256_and_si256(_mm256_srli_epi64(src_hi, 4), clr_mask);
        dst_lo_1 = _mm256_xor_si256(dst_lo_1, _mm256_shuffle_epi8(t3_lo, data_1));
        dst_hi_1 = _mm256_xor_si256(dst_hi_1, _mm256_shuffle_epi8(t3_hi, data_1));
    
        _mm256_storeu_si256((__m256i*)&dst[i], _mm256_xor_si256(*(__m256i*)&dst[i], _mm256_xor_si256(dst_lo_0, dst_lo_1)));
        _mm256_storeu_si256((__m256i*)&dst[i + 32], _mm256_xor_si256(*(__m256i*)&dst[i+32], _mm256_xor_si256(dst_hi_0, dst_hi_1)));
    }
    
    // Handle remaining elements
    for (; i < sz; i++) {
        dst[i] ^= gf_mul_table[(c << 8) + src[i]];
    }
}

void add1_avx2(gf *dst, const gf *src, int sz) {
    int i;
    for (i = 0; i + 64 <= sz; i += 64) {
        __m256i src_lo = _mm256_loadu_si256((__m256i*)&src[i]);
        __m256i src_hi = _mm256_loadu_si256((__m256i*)&src[i + 32]);
        __m256i dst_lo = _mm256_loadu_si256((__m256i*)&dst[i]);
        __m256i dst_hi = _mm256_loadu_si256((__m256i*)&dst[i + 32]);
        _mm256_storeu_si256((__m256i*)&dst[i], _mm256_xor_si256(src_lo, dst_lo));
        _mm256_storeu_si256((__m256i*)&dst[i + 32], _mm256_xor_si256(src_hi, dst_hi));
    }
    
    // Handle remaining elements
    for (; i < sz; i++) {
        dst[i] ^= src[i];
    }
}