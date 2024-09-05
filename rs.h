#include <stdint.h>

#define GF_SIZE 256
#define MAX_DATA_SHARDS 255
#define MAX_TOTAL_SHARDS 255

typedef unsigned char gf; // Galois Field element


// Galois Field tables
extern gf gf_exp[GF_SIZE * 2];
extern int gf_log[GF_SIZE];
extern gf gf_mul_table[GF_SIZE * GF_SIZE];
extern gf gf_div_table[GF_SIZE * GF_SIZE];


// Reed-Solomon structure
typedef struct  {
    int data_shards;
    int parity_shards;
    int shards;
    unsigned char* matrix;
    unsigned char* parity;
} reed_solomon;


// Function declarations
void init_gf();
gf gf_mul(gf a, gf b);
gf gf_div(gf a, gf b);
gf gf_pow(gf a, int n);
gf* vandermonde(int rows, int cols);
gf* vandermonde_submatrix(int rows, int cols, uint8_t* row_list);
gf* cauchy(int rows, int cols);
gf* cauchy_submatrix(int systematic_rows, int rows, int cols, uint8_t* row_list);
static gf* sub_matrix(gf* matrix, int rmin, int cmin, int rmax, int cmax, int nrows, int ncols);
// static gf* multiply1(gf *a, int ar, int ac, gf *b, int br, int bc);
static inline int code_some_shards(gf* matrixRows, gf** inputs, gf** outputs, int dataShards, int outputCount, int byteCount);
reed_solomon* rs_new(int data_shards, int parity_shards);
void rs_encode(reed_solomon* rs, unsigned char** data, unsigned char** parity, int shard_size);
void matrix_multiply(gf* a, gf* b, gf* result, int n);
int is_identity(gf* matrix, int n);
int matrix_invert(gf* matrix, int n);
int rs_decode(reed_solomon* rs, unsigned char** shards, int* erasures, int erasure_count, int shard_size);
void rs_free(reed_solomon* rs);

// AVX2 accelerated functions
void mul1_avx2_orig(gf *dst, const gf *src, gf c, int sz);
void mul1_avx2(gf *dst, const gf *src, gf c, int sz);
void mul_add1_avx2(gf *dst, const gf *src, gf c, int sz);
void add1_avx2(gf *dst, const gf *src, int sz);

// Utility functions
void print_matrix(gf* matrix, int rows, int cols);