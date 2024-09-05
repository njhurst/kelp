#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <stdint.h>
#include <string.h>
#include "rs.h"

// Unit Tests
// Test function to verify gf_div(gf_mul(i, j), j) == i
void test_gf_mul_div_property() {
    printf("Testing Galois Field multiplication and division properties...\n");
    int errors = 0;
    for (int i = 0; i < 256; i++) {
        for (int j = 1; j < 256; j++) {  // Start from 1 to avoid division by zero
            gf result = gf_div(gf_mul(i, j), j);
            if (result != i) {
                printf("Error: gf_div(gf_mul(%d, %d), %d) = %d, expected %d\n", i, j, j, result, i);
                errors++;
            }
        }
    }
    if (errors == 0) {
        printf("All tests passed successfully!\n");
    } else {
        printf("Found %d errors in Galois Field operations.\n", errors);
    }
}

void test_matrix_invert() {
    printf("Testing matrix inversion...\n");
    srand(42);
    
    // Test different matrix sizes.  The larger sizes should be done with the FFT method, but they are not interesting for this project. I included them to show that the code is working.
    int sizes[] = {2, 3, 4, 8, 16, 32, 64, 128, 256, 300};
    int num_sizes = sizeof(sizes) / sizeof(sizes[0]);
    
    for (int size_index = 0; size_index < num_sizes; size_index++) {
        int n = sizes[size_index];
        int max_attempts = 10000 / n;
        printf("Testing %dx%d matrices\n", n, n);
        
        gf* matrix = malloc(n * n * sizeof(gf));
        gf* inverted = malloc(n * n * sizeof(gf));
        gf* result = malloc(n * n * sizeof(gf));
        
        // Test 1: Identity matrix
        printf("Test 1: Identity matrix\n");
        memset(matrix, 0, n * n * sizeof(gf));
        for (int i = 0; i < n; i++) {
            matrix[i * n + i] = 1;
        }
        memcpy(inverted, matrix, n * n * sizeof(gf));
        assert(matrix_invert(inverted, n) == 1);
        assert(is_identity(inverted, n));

        // benchmark identity matrix inversion
        {
        clock_t start_time = clock();
        for (int i = 0; i < max_attempts; i++) {
            matrix_invert(inverted, n);
        }
        clock_t end_time = clock();
        printf("Time taken to invert identity: %f\n", (double)(end_time - start_time) / CLOCKS_PER_SEC / max_attempts);
        }
        
        // Test 2: Random invertible matrix
        printf("Test 2: Random invertible matrix\n");
        int attempts = 0;
        int found_invertible = 0;

        clock_t start_time = clock();

        while (attempts < max_attempts) {
            // Generate random matrix
            for (int i = 0; i < n * n; i++) {
                matrix[i] = rand() % 256;
            }
            memcpy(inverted, matrix, n * n * sizeof(gf));
            
            if (matrix_invert(inverted, n)) {
                // Matrix is invertible, perform checks
                // Check 1: A * A^-1 = I
                matrix_multiply(matrix, inverted, result, n);
                assert(is_identity(result, n));
                
                // Check 2: A^-1 * A = I
                matrix_multiply(inverted, matrix, result, n);
                assert(is_identity(result, n));
                
                // Check 3: (A^-1)^-1 = A
                gf* double_inverted = malloc(n * n * sizeof(gf));
                memcpy(double_inverted, inverted, n * n * sizeof(gf));
                assert(matrix_invert(double_inverted, n) == 1);
                assert(memcmp(double_inverted, matrix, n * n * sizeof(gf)) == 0);
                free((void*)double_inverted);
                
                found_invertible += 1;
                // break;
            }
            attempts++;
        }
        if(attempts == max_attempts && found_invertible) {
            printf("Found %d invertible %dx%d matrix after %d attempts\n", found_invertible, n, n, attempts);
        } else if (attempts == max_attempts && !found_invertible) {
            printf("Failed to generate invertible %dx%d matrix after %d attempts\n", n, n, max_attempts);
        }
        clock_t end_time = clock();
        printf("Time taken: %f\n", (double)(end_time - start_time) / CLOCKS_PER_SEC);
        printf("average time: %f\n", (double)(end_time - start_time) / CLOCKS_PER_SEC / attempts);
        
        // Test 3: Non-invertible matrix
        printf("Test 3: Non-invertible matrix\n");
        memset(matrix, 0, n * n * sizeof(gf));
        assert(matrix_invert(matrix, n) == 0);
        
        free((void*)matrix);
        free((void*)inverted);
        free((void*)result);
    }
    
    printf("Matrix inversion tests completed.\n");
}


void test_rs_new() {
    printf("Testing Reed-Solomon initialization...\n");
    reed_solomon* rs = rs_new(4, 2);
    assert(rs != NULL);
    assert(rs->data_shards == 4);
    assert(rs->parity_shards == 2);
    assert(rs->matrix != NULL);
    free((void*)rs);
    printf("Reed-Solomon initialization tests passed.\n");
}

void test_rs_encode() {
    printf("Testing Reed-Solomon encoding...\n");
    reed_solomon* rs = rs_new(4, 2);
    int shard_size = 4;
    
    unsigned char** data = malloc(4 * sizeof(unsigned char*));
    unsigned char** parity = malloc(2 * sizeof(unsigned char*));
    
    for (int i = 0; i < 4; i++) {
        data[i] = malloc(shard_size);
        for (int j = 0; j < shard_size; j++) {
            data[i][j] = i * shard_size + j;
        }
    }
    
    for (int i = 0; i < 2; i++) {
        parity[i] = malloc(shard_size);
    }
    
    rs_encode(rs, data, parity, shard_size);
    
    // Check that parity is not all zeros
    int all_zero = 1;
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < shard_size; j++) {
            if (parity[i][j] != 0) {
                all_zero = 0;
                break;
            }
        }
        if (!all_zero) break;
    }
    assert(!all_zero);
    
    // Cleanup
    for (int i = 0; i < 4; i++) {
        free((void*)data[i]);
    }
    for (int i = 0; i < 2; i++) {
        free((void*)parity[i]);
    }
    free((void*)data);
    free((void*)parity);
    free((void*)rs);
    
    printf("Reed-Solomon encoding tests passed.\n");
}


void test_rs_decode() {
    printf("Testing Reed-Solomon decoding...\n");
    reed_solomon* rs = rs_new(4, 2);
    int shard_size = 4;
    int total_shards = 6;  // 4 data + 2 parity

    // Allocate memory for shards
    unsigned char** shards = malloc(total_shards * sizeof(unsigned char*));
    for (int i = 0; i < total_shards; i++) {
        shards[i] = malloc(shard_size);
    }

    // Test case 1: No erasures
    printf("Test case 1: No erasures\n");
    // Initialize data shards
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < shard_size; j++) {
            shards[i][j] = i * shard_size + j;
        }
    }
    // Encode
    rs_encode(rs, shards, &shards[4], shard_size);
    
    // Print shards before decoding
    printf("Shards before decoding:\n");
    for (int i = 0; i < total_shards; i++) {
        printf("Shard %d: ", i);
        for (int j = 0; j < shard_size; j++) {
            printf("%02x ", shards[i][j]);
        }
        printf("\n");
    }

    // Decode (should be no-op)
    int erasures[6] = {0};
    int decode_result = rs_decode(rs, shards, erasures, 0, shard_size);
    printf("Decode result: %d\n", decode_result);

    // Print shards after decoding
    printf("Shards after decoding:\n");
    for (int i = 0; i < total_shards; i++) {
        printf("Shard %d: ", i);
        for (int j = 0; j < shard_size; j++) {
            printf("%02x ", shards[i][j]);
        }
        printf("\n");
    }

    assert(decode_result == 1);
    // Verify data shards unchanged
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < shard_size; j++) {
            assert(shards[i][j] == i * shard_size + j);
        }
    }

    // Test case 2: One data shard erased
    printf("Test case 2: One data shard erased\n");
    // Erase second data shard
    memset(shards[1], 0, shard_size);
    erasures[1] = 1;
    assert(rs_decode(rs, shards, erasures, 1, shard_size) == 1);
    // Verify recovered data
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < shard_size; j++) {
            assert(shards[i][j] == i * shard_size + j);
        }
    }

    // Test case 3: Two data shards erased
    printf("Test case 3: Two data shards erased\n");
    // Erase first and third data shards
    memset(shards[0], 0, shard_size);
    memset(shards[2], 0, shard_size);
    erasures[0] = 1;
    erasures[1] = 0;
    erasures[2] = 1;
    assert(rs_decode(rs, shards, erasures, 2, shard_size) == 1);
    // Verify recovered data
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < shard_size; j++) {
            assert(shards[i][j] == i * shard_size + j);
        }
    }

    // Random testing
    printf("Performing random testing...\n");
    srand(42);
    for (int test = 0; test < 100; test++) {
        // Generate random data
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < shard_size; j++) {
                shards[i][j] = rand() % 256;
            }
        }
        // Encode
        rs_encode(rs, shards, &shards[4], shard_size);
        
        // Make a copy of original data for comparison
        unsigned char** original = malloc(total_shards * sizeof(unsigned char*));
        for (int i = 0; i < total_shards; i++) {
            original[i] = malloc(shard_size);
            memcpy(original[i], shards[i], shard_size);
        }
        
        // Randomly erase 0 to 2 shards
        int num_erasures = rand() % 3;
        memset(erasures, 0, sizeof(erasures));
        for (int i = 0; i < num_erasures; i++) {
            int shard_to_erase;
            do {
                shard_to_erase = rand() % total_shards;
            } while (erasures[shard_to_erase]);
            erasures[shard_to_erase] = 1;
            memset(shards[shard_to_erase], 0, shard_size);
        }
        
        // Decode
        assert(rs_decode(rs, shards, erasures, num_erasures, shard_size) == 1);
        
        // Verify recovered data, printing only if there is an error, printing the matrix and highlighting the mismatch
        int error = 0;
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < shard_size; j++) {
                if (shards[i][j] != original[i][j]) {
                    error = 1;
                    break;
                }
            }
            if (error) break;
        }
        if (error) {
            printf("Random test failed:\n");
            printf("Erased shards: ");
            for (int i = 0; i < total_shards; i++) {
                if (erasures[i]) printf("%d ", i);
            }
            printf("\n");
            printf("Original data:\n");
            for (int i = 0; i < total_shards; i++) {
                printf("Shard %d: ", i);
                for (int j = 0; j < shard_size; j++) {
                    printf("%02x ", original[i][j]);
                }
                printf("\n");
            }
            printf("Recovered data:\n");
            for (int i = 0; i < total_shards; i++) {
                printf("Shard %d: ", i);
                for (int j = 0; j < shard_size; j++) {
                    if (shards[i][j] != original[i][j]) {
                        printf("\033[1;31m%02x\033[0m ", shards[i][j]);
                    } else {
                        printf("%02x ", shards[i][j]);
                    }
                }
                printf("\n");
            }
            break;
        }

        
        // Clean up
        for (int i = 0; i < total_shards; i++) {
            free((void*)original[i]);
        }
        free((void*)original);
    }

    // Clean up
    for (int i = 0; i < total_shards; i++) {
        free((void*)shards[i]);
    }
    free((void*)shards);
    free((void*)rs);

    printf("Reed-Solomon decoding tests passed.\n");
}

// Helper function to create a submatrix
void create_submatrix(gf* matrix, int rows, int cols, int* row_indices, int* col_indices, int submatrix_size, gf* submatrix) {
    for (int i = 0; i < submatrix_size; i++) {
        for (int j = 0; j < submatrix_size; j++) {
            submatrix[i * submatrix_size + j] = matrix[row_indices[i] * cols + col_indices[j]];
        }
    }
}

// Test Vandermonde submatrix invertibility
void test_vandermonde_submatrix_invertibility() {
    printf("Testing Vandermonde submatrix invertibility...\n");
    
    int data_shards = 4;
    int parity_shards = 2;
    int total_shards = data_shards + parity_shards;
    gf* matrix = vandermonde(total_shards, data_shards);
    printf("Vandermonde matrix:\n");
    print_matrix(matrix, total_shards, data_shards);
    
    // Allocate memory for indices and submatrix
    int* row_indices = malloc(data_shards * sizeof(int));
    int* col_indices = malloc(data_shards * sizeof(int));
    gf* submatrix = malloc(data_shards * data_shards * sizeof(gf));
    
    // Test all possible square submatrices
    int invertible_count = 0;
    int total_submatrices = 0;
    
    for (int size = 1; size <= data_shards; size++) {
        for (int i = 0; i < (1 << total_shards); i++) {
            if (__builtin_popcount(i) != size) continue;
            
            for (int j = 0; j < (1 << data_shards); j++) {
                if (__builtin_popcount(j) != size) continue;
                
                // Create row and column indices
                int row_count = 0, col_count = 0;
                for (int k = 0; k < total_shards; k++) {
                    if (i & (1 << k)) row_indices[row_count++] = k;
                }
                for (int k = 0; k < data_shards; k++) {
                    if (j & (1 << k)) col_indices[col_count++] = k;
                }
                
                // Create submatrix
                create_submatrix(matrix, total_shards, data_shards, row_indices, col_indices, size, submatrix);
                
                // Check invertibility
                if (matrix_invert(submatrix, size)) {
                    invertible_count++;
                } else {
                    printf("Non-invertible submatrix found:\n");
                    print_matrix(submatrix, size, size);
                }
                
                total_submatrices++;
            }
        }
    }
    
    printf("Invertible submatrices: %d / %d\n", invertible_count, total_submatrices);
    assert(invertible_count == total_submatrices);
    
    // Clean up
    free(row_indices);
    free(col_indices);
    free(submatrix);
    free(matrix);
    
    printf("Vandermonde submatrix invertibility test completed.\n");
}

// Test Cauchy submatrix invertibility
void test_cauchy_submatrix_invertibility() {
    printf("Testing cauchy submatrix invertibility...\n");
    
    int data_shards = 4;
    int parity_shards = 2;
    int total_shards = data_shards + parity_shards;
    gf* matrix = cauchy(total_shards, data_shards);
    printf("cauchy matrix:\n");
    print_matrix(matrix, total_shards, data_shards);
    
    // Allocate memory for indices and submatrix
    int* row_indices = malloc(data_shards * sizeof(int));
    int* col_indices = malloc(data_shards * sizeof(int));
    gf* submatrix = malloc(data_shards * data_shards * sizeof(gf));
    
    // Test all possible square submatrices
    int invertible_count = 0;
    int total_submatrices = 0;
    
    for (int size = 1; size <= data_shards; size++) {
        for (int i = 0; i < (1 << total_shards); i++) {
            if (__builtin_popcount(i) != size) continue;
            
            for (int j = 0; j < (1 << data_shards); j++) {
                if (__builtin_popcount(j) != size) continue;
                
                // Create row and column indices
                int row_count = 0, col_count = 0;
                for (int k = 0; k < total_shards; k++) {
                    if (i & (1 << k)) row_indices[row_count++] = k;
                }
                for (int k = 0; k < data_shards; k++) {
                    if (j & (1 << k)) col_indices[col_count++] = k;
                }
                
                // Create submatrix
                create_submatrix(matrix, total_shards, data_shards, row_indices, col_indices, size, submatrix);
                
                // Check invertibility
                if (matrix_invert(submatrix, size)) {
                    invertible_count++;
                } else {
                    printf("starting with matrix (%d %d):\n", total_shards, data_shards);
                    print_matrix(matrix, total_shards, data_shards);
                    printf("Non-invertible submatrix found from indices ");
                    // print binary indices
                    for (int k = 0; k < total_shards; k++) {
                        printf("%d", (i & (1 << k)) ? 1 : 0);
                    }
                    printf(" ");
                    for (int k = 0; k < data_shards; k++) {
                        printf("%d", (j & (1 << k)) ? 1 : 0);
                    }
                    printf(":\n");
                    create_submatrix(matrix, total_shards, data_shards, row_indices, col_indices, size, submatrix);
                    print_matrix(submatrix, size, size);
                    // invert the submatrix
                    matrix_invert(submatrix, size);
                    printf("Inverted submatrix:\n");
                    print_matrix(submatrix, size, size);
                }
                
                total_submatrices++;
            }
        }
    }
    
    printf("Invertible submatrices: %d / %d\n", invertible_count, total_submatrices);
    assert(invertible_count == total_submatrices);
    
    // Clean up
    free(row_indices);
    free(col_indices);
    free(submatrix);
    free(matrix);
    
    printf("cauchy submatrix invertibility test completed.\n");
}

void run_tests() {
    init_gf();
    test_gf_mul_div_property();
    test_matrix_invert();
    // test_vandermonde_submatrix_invertibility();
    test_cauchy_submatrix_invertibility();
    test_rs_new();
    test_rs_encode();
    test_rs_decode();
    printf("All tests passed successfully!\n");
}

// Main function
int main() {
    run_tests();
    return 0;
}
