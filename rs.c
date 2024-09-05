/**
 * Reed-Solomon Erasure Coding
 * 
 * This is a simple implementation of Reed-Solomon erasure coding in GF(256).  Based on janson's implementation.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <stdint.h>

gf gf_exp[GF_SIZE * 2];
int gf_log[GF_SIZE];
gf gf_mul_table[GF_SIZE * GF_SIZE];
gf gf_div_table[GF_SIZE * GF_SIZE];


gf gf_mul_direct(gf a, gf b);
gf gf_div_direct(gf a, gf b);

// Initialize Galois Field tables
void init_gf() {
    int i, x = 1;
    for (i = 0; i < GF_SIZE - 1; i++) {
        gf_exp[i] = x;
        gf_log[x] = i;
        x = (x << 1) ^ ((x & 0x80) ? 0x1d : 0); // 0x1d is the primitive polynomial
        x &= 0xFF; // Ensure x stays within 8 bits
    }
    gf_exp[GF_SIZE - 1] = 1; // Complete the cycle
    gf_log[0] = -1; // Log of 0 is undefined, use -1 as a sentinel value
    for (i = GF_SIZE; i < GF_SIZE * 2; i++) {
        gf_exp[i] = gf_exp[i - (GF_SIZE - 1)];
    }

    // Generate multiplication table
    for (i = 0; i < GF_SIZE; i++) {
        for (int j = 0; j < GF_SIZE; j++) {
            gf_mul_table[i * GF_SIZE + j] = gf_mul_direct(i, j);
            if(j != 0)
                gf_div_table[i * GF_SIZE + j] = gf_div_direct(i, j);
        }
    }
}

// Galois Field multiplication
gf gf_mul_direct(gf a, gf b) {
    if (a == 0 || b == 0) return 0;
    return gf_exp[(gf_log[a] + gf_log[b]) % (GF_SIZE - 1)];
}

// Galois Field multiplication
// table lookup version, about 2x faster than gf_mul_direct
gf gf_mul(gf a, gf b) {
    return gf_mul_table[a * GF_SIZE + b];
}

// Galois Field division
gf gf_div_direct(gf a, gf b) {
    if (a == 0) return 0;
    if (b == 0) {
        fprintf(stderr, "Error: Division by zero in Galois Field\n");
        exit(1);
    }
    return gf_exp[(gf_log[a] - gf_log[b] + (GF_SIZE - 1)) % (GF_SIZE - 1)];
}

// Galois Field division
gf gf_div(gf a, gf b) {
    return gf_div_table[a * GF_SIZE + b];
}

// Galois Field power
gf gf_pow(gf a, int n) {
    if (n == 0) return 1;
    if (a == 0) return 0;
    return gf_exp[(gf_log[a] * n) % (GF_SIZE - 1)];
}


// Generate a Vandermonde matrix
// The first row is all 1's, the second row is the first row multiplied by a primitive element, and so on
gf* vandermonde(int rows, int cols) {
    gf* matrix = (gf*)malloc(rows * cols * sizeof(gf));
    if (matrix == NULL) {
        return NULL;
    }

    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            if (i == 0 || j == 0) {
                matrix[i * cols + j] = 1;  // First row and column are all 1's
            } else {
                // gf_exp[(i-1) * j % 255] gives us g^((i-1)*j) in GF(256)
                matrix[i * cols + j] = gf_exp[((i) * j) % 255];
            }
        }
    }
    // print_matrix(matrix, rows, cols);

    return matrix;
}

// Generate the submatrix of a vandermonde matrix from a list of rows
// systematic_rows is the number of rows that are not parity, we fill these with the identity matrix
gf* vandermonde_submatrix(int rows, int cols, uint8_t* row_list) {
    gf* matrix = (gf*)malloc(rows * cols * sizeof(gf));
    if (matrix == NULL) {
        return NULL;
    }

    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            if (i == 0 || j == 0) {
                matrix[i * cols + j] = 1;  // First row and column are all 1's
            } else {
                // gf_exp[(i-1) * j % 255] gives us g^((i-1)*j) in GF(256)
                matrix[i * cols + j] = gf_exp[((row_list[i]) * j) % 255];
            }
        }
    }
    // print_matrix(matrix, rows, cols);

    return matrix;
}


// Generate a Cauchy matrix
// 1/(xi+yi), xi, yi taken from invertible elements of GF(256)
gf* cauchy(int rows, int cols) {
    gf* matrix = (gf*)malloc(rows * cols * sizeof(gf));
    if (matrix == NULL) {
        return NULL;
    }

    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            // matrix[i * cols + j] = gf_div(1, i ^ (rows + j));
            matrix[i * cols + j] = gf_div_table[256*1 + i ^ (rows + j)];
        }
    }
    // print_matrix(matrix, rows, cols);

    return matrix;
}

// Generate the submatrix of a cauchy matrix from a list of rows
// systematic_rows is the number of rows that are not parity, we fill these with the identity matrix
gf* cauchy_submatrix(int systematic_rows, int rows, int cols, uint8_t* row_list) {
    gf* matrix = (gf*)malloc(rows * cols * sizeof(gf));
    if (matrix == NULL) {
        return NULL;
    }

    for (int i = 0; i < rows; i++) {
        if (i < systematic_rows) {
            for (int j = 0; j < cols; j++) {
                matrix[i * cols + j] = (i == j) ? 1 : 0;
            }
        } else {
            for (int j = 0; j < cols; j++) {
                matrix[i * cols + j] = gf_div_table[256*1 + row_list[i] ^ (rows + j)];
            }
        }
    }
    // print_matrix(matrix, rows, cols);

    return matrix;
}


// extract a submatrix from a matrix bounded by rmin, cmin, rmax, cmax
static gf* sub_matrix(gf* matrix, int rmin, int cmin, int rmax, int cmax,  int nrows, int ncols) {
    int i, j, ptr = 0;
    gf* new_m = (gf*)malloc( (rmax-rmin) * (cmax-cmin) );
    if(NULL != new_m) {
        for(i = rmin; i < rmax; i++) {
            for(j = cmin; j < cmax; j++) {
                new_m[ptr++] = matrix[i*ncols + j];
            }
        }
    }

    return new_m;
}

// // multiply two matrices in gf(256)

// Helper function to multiply two square matrices
void matrix_multiply(gf* a, gf* b, gf* result, int n) {
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            result[i * n + j] = 0;
            for (int k = 0; k < n; k++) {
                result[i * n + j] ^= gf_mul(a[i * n + k], b[k * n + j]);
            }
        }
    }
}

// // a is r x ac, b is ac x c
// // result is r x c
static gf* multiply1(gf *a, int ar, int ac, gf *b, int br, int bc) {
    gf *new_m, tg;
    int r, c, i, ptr = 0;

    assert(ac == br);
    new_m = (gf*)malloc(ar*bc);
    if(NULL != new_m) {

        /* this multiply is slow */
        for(r = 0; r < ar; r++) {
            for(c = 0; c < bc; c++) {
                tg = 0;
                for(i = 0; i < ac; i++) {
                    tg ^= gf_mul(a[r*ac+i], b[i*bc+c]);
                }

                new_m[ptr++] = tg;
            }
        }

    }

    return new_m;
}

// Helper function to check if a matrix is identity
int is_identity(gf* matrix, int n) {
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            if (i == j) {
                if (matrix[i * n + j] != 1) return 0;
            } else {
                if (matrix[i * n + j] != 0) return 0;
            }
        }
    }
    return 1;
}

// Matrix inversion
// Inverts a square matrix in GF(256)
// Returns 1 if successful, 0 if the matrix is not invertible
// Uses Gauss-Jordan elimination
// The input matrix is modified in place
// Optimised to skip identity rows, so is essentially a no-op for the identity matrix
int matrix_invert(gf* matrix, int n) {
    gf temp;
    int i, j, k;
    gf* inverse = malloc(n * n * sizeof(gf));
    memset(inverse, 0, n * n * sizeof(gf));
    
    // Initialize inverse as identity matrix
    for (i = 0; i < n; i++) {
        inverse[i * n + i] = 1;
    }
    
    // Gauss-Jordan elimination
    for (i = 0; i < n; i++) {
        // Find pivot
        if (matrix[i * n + i] == 0) {
            for (j = i + 1; j < n; j++) {
                if (matrix[j * n + i] != 0) {
                    for (k = 0; k < n; k++) {
                        temp = matrix[i * n + k];
                        matrix[i * n + k] = matrix[j * n + k];
                        matrix[j * n + k] = temp;
                        
                        temp = inverse[i * n + k];
                        inverse[i * n + k] = inverse[j * n + k];
                        inverse[j * n + k] = temp;
                    }
                    break;
                }
            }
            if (j == n) {
                free((void*)inverse);
                return 0; // Matrix is not invertible
            }
        }
        
        // Scale row
        // temp = gf_div(1, matrix[i * n + i]);
        if (matrix[i * n + i] != 1) {
            temp = gf_div_table[256*1 + matrix[i * n + i]];
            for (j = 0; j < n; j++) {
                matrix[i * n + j] = gf_mul(matrix[i * n + j], temp);
                inverse[i * n + j] = gf_mul(inverse[i * n + j], temp);
            }
        }

        // Eliminate
        for (j = 0; j < n; j++) {
            if (i != j) {
                temp = matrix[j * n + i];
                if (temp != 0) {
                    for (k = 0; k < n; k++) {
                        matrix[j * n + k] ^= gf_mul(temp, matrix[i * n + k]);
                        inverse[j * n + k] ^= gf_mul(temp, inverse[i * n + k]);
                    }
                }
            }
        }
    }
    
    memcpy(matrix, inverse, n * n * sizeof(gf));
    free((void*)inverse);
    return 1;
}


const int DATA_SHARDS_MAX = 255;

reed_solomon* rs_new(int data_shards, int parity_shards) {
    gf* vm = NULL;
    gf* top = NULL;
    int err = 0;
    reed_solomon* rs = NULL;

    /* MUST use fec_init once time first */
    // assert(fec_initialized);

    do {
        rs = (reed_solomon*) malloc(sizeof(reed_solomon));
        if(NULL == rs) {
            return NULL;
        }
        rs->data_shards = data_shards;
        rs->parity_shards = parity_shards;
        rs->shards = (data_shards + parity_shards);
        rs->matrix = NULL;
        rs->parity = NULL;

        if(rs->shards > DATA_SHARDS_MAX || data_shards <= 0 || parity_shards <= 0) {
            err = 1;
            break;
        }

        // vm = vandermonde(rs->shards, rs->data_shards);
        vm = cauchy(rs->shards, rs->data_shards);
        if(NULL == vm) {
            err = 2;
            break;
        }
        printf("Vandermonde matrix:\n");
        print_matrix(vm, rs->shards, rs->data_shards);

        top = sub_matrix(vm, 0, 0, data_shards, data_shards, rs->shards, data_shards);
        if(NULL == top) {
            err = 3;
            break;
        }
        printf("Top submatrix:\n");
        print_matrix(top, data_shards, data_shards);

        err = matrix_invert(top, data_shards);
        if (err == 0) {
            printf("Matrix inversion failed with error code: %d\n", err);
            break;
        }
        printf("Inverted matrix:\n");
        print_matrix(top, data_shards, data_shards);

        rs->matrix = multiply1(vm, rs->shards, data_shards, top, data_shards, data_shards);
        // rs->matrix = (gf*)malloc(rs->shards * data_shards);
        // matrix_multiply(vm, top, rs->matrix, rs->shards);
        if(NULL == rs->matrix) {
            err = 4;
            break;
        }

        rs->parity = sub_matrix(rs->matrix, data_shards, 0, rs->shards, data_shards, rs->shards, data_shards);
        if(NULL == rs->parity) {
            err = 5;
            break;
        }

        free((void*)vm);
        free((void*)top);
        vm = NULL;
        top = NULL;
        return rs;

    } while(0);

    fprintf(stderr, "err=%d\n", err);
    if(NULL != vm) {
        free((void*)vm);
    }
    if(NULL != top) {
        free((void*)top);
    }
    if(NULL != rs) {
        if(NULL != rs->matrix) {
            free((void*)rs->matrix);
        }
        if(NULL != rs->parity) {
            free((void*)rs->parity);
        }
        free((void*)rs);
    }

    return NULL;
}

// Encode data
// data is an array of pointers to the data shards
// parity is an array of pointers to the parity shards
void rs_encode(reed_solomon* rs, unsigned char** data, unsigned char** parity, int shard_size) {
    for (int i = 0; i < rs->parity_shards; i++) {
        int first = 1;
        for (int j = 0; j < rs->data_shards; j++) {
            gf coeff = rs->matrix[(rs->data_shards + i) * rs->data_shards + j];
            if(coeff == 0) continue;
            if (first) {
                first = 0;
                if(coeff == 1) {
                    memcpy(parity[i], data[j], shard_size);
                } else {
                    mul1_avx2(parity[i], data[j], coeff, shard_size);
                }
            } else {
                if(coeff == 1) {
                    add1_avx2(parity[i], data[j], shard_size);
                } else {
                    mul_add1_avx2(parity[i], data[j], coeff, shard_size);
                }
            }
        }
    }
}


// Decode data
int rs_decode(reed_solomon* rs, unsigned char** shards, int* erasures, int erasure_count, int shard_size) {
    int i, j, k;
    int data_shards = rs->data_shards;
    int total_shards = data_shards + rs->parity_shards;
    
    // Check if we have enough shards to reconstruct
    if (total_shards - erasure_count < data_shards) {
        printf("Not enough shards to reconstruct data\n");
        return 0; // Not enough shards to reconstruct
    }
    
    // Create submatrix
    gf* submatrix = malloc(data_shards * data_shards * sizeof(gf));
    int* valid_shards = malloc(data_shards * sizeof(int));
    int valid_count = 0;
    
    for (i = 0; i < total_shards && valid_count < data_shards; i++) {
        if (erasures[i] == 0) {
            for (j = 0; j < data_shards; j++) {
                submatrix[valid_count * data_shards + j] = rs->matrix[i * data_shards + j];
            }
            valid_shards[valid_count] = i;
            valid_count++;
        }
    }
    
    // Invert the submatrix
    if (!matrix_invert(submatrix, data_shards)) {
        free((void*)submatrix);
        free((void*)valid_shards);
        print_matrix(rs->matrix, total_shards, data_shards);
        printf("Submatrix is not invertible\n");
        return 0; // Submatrix is not invertible
    }
    
    // Reconstruct missing shards
    for (i = 0; i < total_shards; i++) {
        if (erasures[i] == 1) {
            memset(shards[i], 0, shard_size);
            for (j = 0; j < data_shards; j++) {
                gf* shard = shards[valid_shards[j]];
                gf coeff = submatrix[i * data_shards + j];
                mul_add1_avx2(shards[i], shard, coeff, shard_size);
                // for (k = 0; k < shard_size; k++) {
                //     shards[i][k] ^= gf_mul(coeff, shard[k]);
                // }
            }
        }
    }
    
    free((void*)submatrix);
    free((void*)valid_shards);
    return 1;
}

// General purpose gf(256) coding: given a list of input shard_ids and a list of output shard_ids,
// compute the coding coefficients and perform the coding operation.
// The input shards are read-only, and the output shards are modified in place.
// The input and output lists must be disjoint.
// The input must contain a linear span of the output shards. (partial decode is fine as long as the input is a subset of the output)
// The input and output lists may be in any order.
// input and outputs can include both data and parity shards.
// the code should handle various identity cases fast enough to need special casing
// the length of shard_ids and shards is input_count + output_count

int rs_generic_galois_coding(reed_solomon *rs, int* shard_ids, int input_count, int output_count, size_t shard_size, unsigned char** shards) {
    int i, j, k;
    int data_shards = input_count;
    int total_shards = input_count + output_count;
    gf* input_matrix = malloc(data_shards * data_shards * sizeof(gf));
    gf* output_matrix = malloc(output_count * data_shards * sizeof(gf));
    
    // Create submatrix
    for (i = 0; i < data_shards; i++) {
        if (shard_ids[i] < data_shards) {
            memcpy(&input_matrix[i * data_shards], &rs->matrix[shard_ids[i] * data_shards], data_shards);
        }
    }
    
    // Invert the submatrix
    if (!matrix_invert(input_matrix, data_shards)) {
        free((void*)input_matrix);
        free((void*)output_matrix);
        return 0; // Submatrix is not invertible
    }

    // construct output matrix
    for (i = 0; i < output_count; i++) {
        memcpy(&output_matrix[i * data_shards], &rs->matrix[(data_shards + shard_ids[i]) * data_shards], data_shards);
    }
    // multiply the output shards by the inverse matrix
    gf* reconstruction_matrix = malloc(output_count * data_shards * sizeof(gf));
    matrix_multiply(output_matrix, input_matrix, reconstruction_matrix, output_count);
    free((void*)output_matrix);
    output_matrix = NULL;
    
    
    // Reconstruct missing shards
    for (int i = 0; i < output_count; i++) {
        int first = 1;
        for (int j = 0; j < data_shards; j++) {
            int coeff = reconstruction_matrix[i * data_shards + j];
            if(coeff == 0) continue;
            if (first) {
                first = 0;
                if(coeff == 1) {
                    memcpy(shards[i], shards[j], shard_size);
                } else {
                    mul1_avx2(shards[i], shards[j], coeff, shard_size);
                }
            } else {
                if(coeff == 1) {
                    add1_avx2(shards[i], shards[j], shard_size);
                } else {
                    mul_add1_avx2(shards[i], shards[j], coeff, shard_size);
                }
            }
        }
    }
    
    free((void*)input_matrix);
    free((void*)reconstruction_matrix);
    
    return 1;
}


// Free Reed-Solomon codec
void rs_free(reed_solomon* rs) {
    free((void*)rs->matrix);
    free((void*)rs);
}


// Helper function to print a n x m matrix
void print_matrix(gf* matrix, int n, int m) {
    for (int i = 0; i < n; i++) {
        printf("Row %d: ", i);
        for (int j = 0; j < m; j++) {
            printf("%02x ", matrix[i * m + j]);
        }
        printf("\n");
    }
}

