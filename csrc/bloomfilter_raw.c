#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include "hash.h"

typedef struct {
    uint64_t size;
    uint64_t hash_count;
    uint64_t items_count;
    uint8_t *bit_array;
} BloomFilter;

BloomFilter* bloom_filter_create(uint64_t size, uint64_t hash_count) {
    if (size == 0 || hash_count == 0) {
        return NULL;
    }
    BloomFilter *bf = (BloomFilter*)malloc(sizeof(BloomFilter));
    if (!bf) {
        return NULL;
    }
    bf->size = size;
    bf->hash_count = hash_count;
    bf->items_count = 0;
    bf->bit_array = (uint8_t*)calloc((size + 7) / 8, sizeof(uint8_t));
    if (!bf->bit_array) {
        free(bf);
        return NULL;
    }
    return bf;
}

void bloom_filter_free(BloomFilter *bf) {
    if (bf) {
        if (bf->bit_array) {
            free(bf->bit_array);
        }
        free(bf);
    }
}

BloomFilter* bloom_filter_from_expected(uint64_t expected_elements, double false_positive_rate) {
    if (expected_elements == 0 || false_positive_rate <= 0.0 || false_positive_rate >= 1.0) {
        return NULL;
    }
    double ln2 = 0.6931471805599453;
    double size_d = -(expected_elements * log(false_positive_rate)) / (ln2 * ln2);
    uint64_t size = (uint64_t)ceil(size_d);
    if (size < 1) size = 1;
    double hash_count_d = (size / (double)expected_elements) * ln2;
    uint64_t hash_count = (uint64_t)round(hash_count_d);
    if (hash_count < 1) hash_count = 1;
    return bloom_filter_create(size, hash_count);
}

static inline void set_bit(uint8_t *array, uint64_t index) {
    array[index >> 3] |= (1 << (index & 7));
}

static inline bool get_bit(const uint8_t *array, uint64_t index) {
    return (array[index >> 3] & (1 << (index & 7))) != 0;
}

static inline uint64_t double_hash(uint64_t h1, uint64_t h2, uint64_t i, uint64_t size) {
    return (h1 + i * h2) % size;
}

void bloom_filter_add(BloomFilter *bf, const char *item) {
    if (!bf || !item) {
        return;
    }
    uint64_t seed1 = 0x123456789ABCDEF0ULL;
    uint64_t seed2 = 0xFEDCBA9876543210ULL;
    uint64_t h1 = murmur3_64_string(item, seed1);
    uint64_t h2 = murmur3_64_string(item, seed2);
    if (h2 == 0) h2 = 1;
    bool was_new = false;
    for (uint64_t i = 0; i < bf->hash_count; i++) {
        uint64_t idx = double_hash(h1, h2, i, bf->size);
        if (!get_bit(bf->bit_array, idx)) {
            was_new = true;
            set_bit(bf->bit_array, idx);
        }
    }
    if (was_new) {
        bf->items_count++;
    }
}

bool bloom_filter_contains(const BloomFilter *bf, const char *item) {
    if (!bf || !item) {
        return false;
    }
    uint64_t seed1 = 0x123456789ABCDEF0ULL;
    uint64_t seed2 = 0xFEDCBA9876543210ULL;
    uint64_t h1 = murmur3_64_string(item, seed1);
    uint64_t h2 = murmur3_64_string(item, seed2);
    if (h2 == 0) h2 = 1;
    for (uint64_t i = 0; i < bf->hash_count; i++) {
        uint64_t idx = double_hash(h1, h2, i, bf->size);
        if (!get_bit(bf->bit_array, idx)) {
            return false;
        }
    }
    return true;
}

double bloom_filter_fill_ratio(const BloomFilter *bf) {
    if (!bf || bf->size == 0) {
        return 0.0;
    }
    uint64_t filled = 0;
    uint64_t bytes = (bf->size + 7) / 8;
    for (uint64_t i = 0; i < bytes; i++) {
        uint8_t byte = bf->bit_array[i];
        while (byte) {
            filled += byte & 1;
            byte >>= 1;
        }
    }
    return (double)filled / (double)bf->size;
}

double bloom_filter_current_fp_rate(const BloomFilter *bf) {
    if (!bf || bf->items_count == 0) {
        return 0.0;
    }
    double exponent = -((double)bf->hash_count * (double)bf->items_count) / (double)bf->size;
    return pow(1.0 - exp(exponent), (double)bf->hash_count);
}

void bloom_filter_stats(const BloomFilter *bf) {
    if (!bf) {
        return;
    }
    printf("{\n");
    printf("  \"size\": %llu,\n", (unsigned long long)bf->size);
    printf("  \"hash_count\": %llu,\n", (unsigned long long)bf->hash_count);
    printf("  \"items_added\": %llu,\n", (unsigned long long)bf->items_count);
    printf("  \"fill_ratio\": %f,\n", bloom_filter_fill_ratio(bf));
    printf("  \"estimated_fp_rate\": %f\n", bloom_filter_current_fp_rate(bf));
    printf("}\n");
}

int main(void) {
    BloomFilter *bf = bloom_filter_from_expected(1000, 0.01);
    if (!bf) {
        return 1;
    }
    printf("Optimal size: %llu\n", (unsigned long long)bf->size);
    printf("Optimal hash count: %llu\n", (unsigned long long)bf->hash_count);

    const char *words[] = {"apple", "banana", "orange", "grape", "kiwi", "mango", "peach"};
    for (size_t i = 0; i < 7; i++) {
        bloom_filter_add(bf, words[i]);
    }

    printf("\n'watermelon' in filter: %s\n",
           bloom_filter_contains(bf, "watermelon") ? "true" : "false");
    printf("Statistics: ");
    bloom_filter_stats(bf);

    bloom_filter_free(bf);

    BloomFilter *bf2 = bloom_filter_create(20, 2);
    if (!bf2) {
        return 1;
    }
    const char *fruits[] = {
        "apple", "banana", "orange", "grape", "kiwi",
        "mango", "peach", "pear", "plum", "cherry"
    };
    for (size_t i = 0; i < 10; i++) {
        bloom_filter_add(bf2, fruits[i]);
    }

    printf("\nSmall filter: 'watermelon' in filter: %s\n",
           bloom_filter_contains(bf2, "watermelon") ? "true" : "false");
    printf("Small filter statistics: ");
    bloom_filter_stats(bf2);

    bloom_filter_free(bf2);
    return 0;
}
