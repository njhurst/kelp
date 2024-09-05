#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <immintrin.h>
#include <x86intrin.h>

// Original version
void spread_data_original(void *input, void **output_blocks, size_t input_size, int k) {
    unsigned char *src = (unsigned char *)input;
    unsigned char **dest = (unsigned char **)output_blocks;
    size_t bytes_copied = 0;
    int current_block = 0;
    size_t offset[k];

    for (int i = 0; i < k; i++) {
        offset[i] = 0;
    }

    while (bytes_copied < input_size) {
        size_t bytes_to_copy = (input_size - bytes_copied < 16) ? (input_size - bytes_copied) : 16;
        memcpy(dest[current_block] + offset[current_block], src + bytes_copied, bytes_to_copy);
        bytes_copied += bytes_to_copy;
        offset[current_block] += bytes_to_copy;
        current_block = (current_block + 1) % k;
    }
}

// SIMD-optimized version
void spread_data_simd(void *input, void **output_blocks, size_t input_size, int k) {
    unsigned char *src = (unsigned char *)input;
    unsigned char **dest = (unsigned char **)output_blocks;
    size_t offset[k];
    
    for (int i = 0; i < k; i++) {
        offset[i] = 0;
    }

    while (input_size >= 16 * k) {
        for (int i = 0; i < k; i++) {
            __m128i data = _mm_loadu_si128((__m128i*)src);
            _mm_storeu_si128((__m128i*)(dest[i] + offset[i]), data);
            src += 16;
            offset[i] += 16;
        }
        input_size -= 16 * k;
    }

    int current_block = 0;
    while (input_size > 0) {
        size_t bytes_to_copy = (input_size < 16) ? input_size : 16;
        memcpy(dest[current_block] + offset[current_block], src, bytes_to_copy);
        src += bytes_to_copy;
        offset[current_block] += bytes_to_copy;
        input_size -= bytes_to_copy;
        current_block = (current_block + 1) % k;
    }
}

// Benchmark function
double benchmark(void (*func)(void*, void**, size_t, int), void *input, void **output_blocks, size_t input_size, int k, int iterations) {
    clock_t start, end;
    double cpu_time_used;

    start = clock();
    for (int i = 0; i < iterations; i++) {
        func(input, output_blocks, input_size, k);
    }
    end = clock();

    // compare output_blocks against the original output_blocks
    // make copy of output_blocks
    void **output_blocks_copy = malloc(k * sizeof(void*));
    const int output_size = ((input_size / k) + 0x1f) & ~0x1F;
    for (int i = 0; i < k; i++) {
        output_blocks_copy[i] = malloc(output_size);
        memset(output_blocks_copy[i], 0, output_size);
        memcpy(output_blocks_copy[i], output_blocks[i], output_size);
    }

    // call original function
    void **output_blocks_original = malloc(k * sizeof(void*));
    for (int i = 0; i < k; i++) {
        output_blocks_original[i] = malloc(output_size);
    }
    spread_data_original(input, output_blocks_original, input_size, k);

    // compare output_blocks and output_blocks_original
    for (int i = 0; i < k; i++) {
        if (memcmp(output_blocks_copy[i], output_blocks_original[i], output_size) != 0) {
            printf("Error: output_blocks and output_blocks_original are not equal\n");
            // print first 16 bytes of each block
            for (int j = 0; j < k; j++) {
                printf("Block %d:\n original:   ", j);
                for (int l = 0; l < 16; l++) {
                    printf("%02x", ((unsigned char*)output_blocks_original[j])[l]);
                }
                printf("...");
                for (int l = output_size - 16; l < output_size; l++) {
                    printf("%02x", ((unsigned char*)output_blocks_original[j])[l]);
                }
                printf("\n optimised:  ");
                for (int l = 0; l < 16; l++) {
                    printf("%02x", ((unsigned char*)output_blocks_copy[j])[l]);
                }
                printf("...");
                for (int l = output_size - 16; l < output_size; l++) {
                    printf("%02x", ((unsigned char*)output_blocks_copy[j])[l]);
                }
                printf("\n");
                // show the point where the two blocks differ ( 16 bytes before and after)
                for (int l = 0; l < output_size; l++) {
                    if (((unsigned char*)output_blocks_copy[j])[l] != ((unsigned char*)output_blocks_original[j])[l]) {
                        printf("Difference at index %d\n", l);
                        for (int m = l - 16; m < l + 16; m++) {
                            printf("%02x", ((unsigned char*)output_blocks_original[j])[m]);
                        }
                        
                        printf("\n");
                        // put caret at the point of difference
                        for (int m = l - 16; m < l; m++) {
                            printf("  ");
                        }
                        printf("^^");
                        for (int m = l + 1; m < l + 16; m++) {
                            printf("  ");
                        }
                        printf("\n");
                        for (int m = l - 16; m < l + 16; m++) {
                            printf("%02x", ((unsigned char*)output_blocks_copy[j])[m]);
                        }
                        printf("\n");
                        break;
                    }
                }
            }
            break;
        }
    }

    // free memory
    for (int i = 0; i < k; i++) {
        free(output_blocks_copy[i]);
        free(output_blocks_original[i]);
    }
    free(output_blocks_copy);
    free(output_blocks_original);



    cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
    return cpu_time_used / iterations;
}

int main() {
    const int NUM_SIZES = 4;
    // const size_t input_sizes[] = {1024, 10240, 102400, 1024000, 4096 * 8};
    const size_t input_sizes[] = {4096};
    const int NUM_K = 5;
    const int k_values[] = {1, 2, 4, 8, 16};
    const int iterations = 1000;

    for (int size_index = 0; size_index < NUM_SIZES; size_index++) {
        size_t input_size = input_sizes[size_index];
        void *input_alloc = malloc(input_size*16 + 37);
        void *input = input_alloc + 37; // Align input to 37 bytes
        
        // Initialize input with random data
        for (size_t i = 0; i < input_size*16; i++) {
            ((unsigned char*)input)[i] = rand() % 256;
        }

        // print first and last 16 bytes of input
        // printf("Input: ");
        // for (int i = 0; i < 16; i++) {
        //     printf("%02x", ((unsigned char*)input)[i]);
        // }
        // printf("...");
        // for (int i = input_size*16 - 16; i < input_size*16; i++) {
        //     printf("%02x", ((unsigned char*)input)[i]);
        // }
        // printf("\n");

        printf("Input size: %zu bytes\n", input_size);

        for (int k_index = 0; k_index < NUM_K; k_index++) {
            int k = k_values[k_index];
            void **output_blocks = malloc(k * sizeof(void*));
            for (int i = 0; i < k; i++) {
                output_blocks[i] = malloc(input_size);
            }

            printf("  Number of blocks (k): %d\n", k);

            double time_original = benchmark(spread_data_original, input, output_blocks, input_size, k, iterations) * 1000000;
            double time_simd = benchmark(spread_data_simd, input, output_blocks, input_size, k, iterations) * 1000000;
            double data_size_B = (double)input_size;

            printf("data_size: %f\n", data_size_B);

            printf("    Original:       %f us = %f GB/s\n", time_original, data_size_B / time_original);
            printf("    SIMD:           %f us (%.2f%% faster) = %f GB/s\n", time_simd, (time_original/time_simd - 1) * 100, data_size_B / time_simd);

            for (int i = 0; i < k; i++) {
                free(output_blocks[i]);
            }
            free(output_blocks);
        }

        free(input_alloc);
        printf("\n");
    }

    // // show the output for a single run of the original function on just 100 bytes into 4 blocks with input data of 0x00 to 0x64
    // const int k = 4;
    // const block_size = 32;
    // void *input = malloc(block_size*k);
    // for (size_t i = 0; i < block_size*k; i++) {
    //     ((unsigned char*)input)[i] = i % 0xff;
    // }
    // void **output_blocks = malloc(k * sizeof(void*));
    // for (int i = 0; i < k; i++) {
    //     output_blocks[i] = malloc(block_size);
    // }
    // spread_data_original(input, output_blocks, 100, k);
    // for (int i = 0; i < k; i++) {
    //     printf("Block %d: ", i);
    //     for (int j = 0; j < block_size; j+=16) {
    //         for(int b = 0; b < 16; b++) {
    //             printf("%02x", ((unsigned char*)output_blocks[i])[j+b]);
    //         }
    //         printf(" ");
    //     }
    //     printf("\n");
    // }

    return 0;
}