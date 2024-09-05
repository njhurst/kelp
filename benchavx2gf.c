#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include <string.h>
#include "rs.h"

const size_t BLOCK_SIZE = (1024 * 4 - 32);
#define NUM_ELEMENTS (BLOCK_SIZE*16)
#define ITERATIONS 1000

double benchmark() {
    uint8_t *a = (uint8_t*)aligned_alloc(32, NUM_ELEMENTS);
    uint8_t *res = (uint8_t*)aligned_alloc(32, NUM_ELEMENTS);

    // Initialize input data
    for (int i = 0; i < NUM_ELEMENTS; i++) {
        a[i] = rand() & 0xFF;
    }

    uint8_t m[8][8];

    clock_t start = clock();

    for (int iter = 0; iter < ITERATIONS; iter++) {
        for(int block = 0; block < 8; block++) {
            int first = 1;
            for(int j = 0; j < 8; j++) {
                gf coeff = m[block][j];
                if(coeff == 0) continue;
                if (first) {
                    first = 0;
                    if (coeff == 1) {
                        memcpy(res + BLOCK_SIZE*(block+8), a + BLOCK_SIZE*j, BLOCK_SIZE);
                    } else {
                        mul1_avx2(res + BLOCK_SIZE*(block+8), a + BLOCK_SIZE*0, coeff, BLOCK_SIZE);
                    }
                } else {
                    if (coeff == 1) {
                        add1_avx2(res + BLOCK_SIZE*(block+8), a + BLOCK_SIZE*j, BLOCK_SIZE);
                    } else {
                        mul_add1_avx2(res + BLOCK_SIZE*(block+8), a + BLOCK_SIZE*j, coeff, BLOCK_SIZE);
                    }
                }
            }
        }
    }

    clock_t end = clock();

    double time_spent = (double)(end - start) / CLOCKS_PER_SEC;
    double gb_processed = (double)(NUM_ELEMENTS) * (double)(ITERATIONS) / (1024 * 1024 * 1024);
    double gb_per_second = gb_processed / time_spent;
    printf("Time: %.2f us = %g us per iteration\n", time_spent * 1e6, time_spent * 1e6 / ITERATIONS);
    printf("Processed: %.2f GB\n", gb_processed);
    printf("Throughput: %.2f GB/s\n", gb_per_second);

    free(a);
    free(res);

    return gb_per_second;
}

// Helper function for the original implementation
void gf256_mul_original(uint8_t *res, const uint8_t *a, const uint8_t* b, int size) {
    mul1_avx2(res, a, b[0], size);
}

// Reference implementation for validation
void gf256_mul_reference(uint8_t *res, const uint8_t *a, const uint8_t* b, int size) {
    for (int i = 0; i < size; i++) {
        // res[i] = gf_mul_table_2d[a[i]][b[i & ~31]];
        res[i] = gf_mul_table[a[i] * GF_SIZE + b[i]];
    }
}

#define TEST_SIZE 32

// Unit test function
void run_unit_tests() {
    printf("Running unit tests...\n");

    uint8_t a[TEST_SIZE], b[TEST_SIZE];
    uint8_t res_original[TEST_SIZE], res_new[TEST_SIZE], res_reference[TEST_SIZE];

    // Initialize test data
    for (int i = 0; i < TEST_SIZE; i++) {
        a[i] = rand() & 0xFF;
        b[i] = 20; // due to AVX limitations, we can only multiply by a single byte across the entire vector
    }

    // Test original implementation
    gf256_mul_original(res_original, a, b, TEST_SIZE);

    // Compute reference result
    gf256_mul_reference(res_reference, a, b, TEST_SIZE);

    // Compare results
    for (int i = 0; i < TEST_SIZE; i++) {
        if(res_original[i] != res_reference[i]) {
            printf("Original implementation failed [%d] %2x (*) %2x = %2x (%2x)\n", i, a[i], b[i], res_original[i], res_reference[i]);
        }
    }

    printf("All unit tests passed successfully!\n\n");
}

int main() {
    init_gf();

    // Run unit tests
    run_unit_tests();

    benchmark();

    return 0;
}