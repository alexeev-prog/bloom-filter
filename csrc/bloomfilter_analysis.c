#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <time.h>
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

typedef struct {
    uint64_t expected_elements;
    double target_fp_rate;
    uint64_t actual_size;
    uint64_t hash_count;
    uint64_t items_added;
    double fill_ratio;
    double estimated_fp_rate;
    double measured_fp_rate;
    double time_ms;
} TestResult;

double measure_false_positives(BloomFilter *bf, const char **negative_items, uint64_t count) {
    if (!bf || !negative_items || count == 0) {
        return 0.0;
    }
    uint64_t false_positives = 0;
    for (uint64_t i = 0; i < count; i++) {
        if (bloom_filter_contains(bf, negative_items[i])) {
            false_positives++;
        }
    }
    return (double)false_positives / (double)count;
}

char* generate_random_string(uint64_t length) {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    char *str = (char*)malloc(length + 1);
    if (!str) {
        return NULL;
    }
    for (uint64_t i = 0; i < length; i++) {
        str[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    str[length] = '\0';
    return str;
}

void generate_unique_strings(char **strings, uint64_t count, uint64_t length) {
    for (uint64_t i = 0; i < count; i++) {
        strings[i] = generate_random_string(length);
    }
}

void free_strings(char **strings, uint64_t count) {
    for (uint64_t i = 0; i < count; i++) {
        if (strings[i]) {
            free(strings[i]);
        }
    }
}

int compare_double(const void *a, const void *b) {
    double diff = *(double*)a - *(double*)b;
    return (diff > 0) - (diff < 0);
}

double median(double *arr, uint64_t n) {
    qsort(arr, n, sizeof(double), compare_double);
    if (n % 2 == 0) {
        return (arr[n/2 - 1] + arr[n/2]) / 2.0;
    }
    return arr[n/2];
}

void run_test_suite(const char *filename) {
    FILE *file = fopen(filename, "w");
    if (!file) {
        fprintf(stderr, "Cannot open %s for writing\n", filename);
        return;
    }

    fprintf(file, "test_id,run_id,scenario,expected_elements,target_fp_rate,actual_size,hash_count,items_added,fill_ratio,estimated_fp_rate,measured_fp_rate,time_ms\n");

    srand((unsigned int)time(NULL));

    uint64_t test_id = 0;
    uint64_t runs_per_scenario = 10;

    struct {
        uint64_t elements;
        double fp_rate;
        const char *scenario;
    } scenarios[] = {
        {100, 0.01, "small_low_fp"},
        {100, 0.05, "small_high_fp"},
        {500, 0.01, "medium_low_fp"},
        {500, 0.05, "medium_high_fp"},
        {1000, 0.01, "large_low_fp"},
        {1000, 0.05, "large_high_fp"},
        {5000, 0.01, "xlarge_low_fp"},
        {5000, 0.05, "xlarge_high_fp"},
        {10000, 0.01, "huge_low_fp"},
        {10000, 0.05, "huge_high_fp"}
    };

    uint64_t num_scenarios = sizeof(scenarios) / sizeof(scenarios[0]);

    for (uint64_t s = 0; s < num_scenarios; s++) {
        uint64_t expected = scenarios[s].elements;
        double target_fp = scenarios[s].fp_rate;
        const char *scenario = scenarios[s].scenario;

        char **train_items = (char**)malloc(expected * sizeof(char*));
        if (!train_items) continue;

        generate_unique_strings(train_items, expected, 10);

        uint64_t test_count = expected * 10;
        if (test_count < 1000) test_count = 1000;

        char **test_items = (char**)malloc(test_count * sizeof(char*));
        if (!test_items) {
            free_strings(train_items, expected);
            free(train_items);
            continue;
        }

        generate_unique_strings(test_items, test_count, 10);

        double *fp_rates = (double*)malloc(runs_per_scenario * sizeof(double));
        double *times = (double*)malloc(runs_per_scenario * sizeof(double));
        if (!fp_rates || !times) {
            free_strings(train_items, expected);
            free(train_items);
            free_strings(test_items, test_count);
            free(test_items);
            if (fp_rates) free(fp_rates);
            if (times) free(times);
            continue;
        }

        for (uint64_t run = 0; run < runs_per_scenario; run++) {
            BloomFilter *bf = bloom_filter_from_expected(expected, target_fp);
            if (!bf) continue;

            clock_t start = clock();

            for (uint64_t i = 0; i < expected; i++) {
                bloom_filter_add(bf, train_items[i]);
            }

            double measured_fp = measure_false_positives(bf, (const char**)test_items, test_count);

            clock_t end = clock();
            double time_ms = ((double)(end - start) / CLOCKS_PER_SEC) * 1000.0;

            fp_rates[run] = measured_fp;
            times[run] = time_ms;

            fprintf(file, "%llu,%llu,%s,%llu,%f,%llu,%llu,%llu,%f,%f,%f,%f\n",
                    (unsigned long long)test_id,
                    (unsigned long long)run,
                    scenario,
                    (unsigned long long)expected,
                    target_fp,
                    (unsigned long long)bf->size,
                    (unsigned long long)bf->hash_count,
                    (unsigned long long)bf->items_count,
                    bloom_filter_fill_ratio(bf),
                    bloom_filter_current_fp_rate(bf),
                    measured_fp,
                    time_ms);

            test_id++;
            bloom_filter_free(bf);
        }

        double median_fp = median(fp_rates, runs_per_scenario);
        double median_time = median(times, runs_per_scenario);

        printf("Scenario %s: median FP=%.6f, median time=%.2fms (runs=%llu)\n",
               scenario, median_fp, median_time, (unsigned long long)runs_per_scenario);

        free_strings(train_items, expected);
        free(train_items);
        free_strings(test_items, test_count);
        free(test_items);
        free(fp_rates);
        free(times);
    }

    fclose(file);
    printf("\nResults saved to %s\n", filename);
}

int main(void) {
    run_test_suite("results.csv");
    return 0;
}
