#include <stdio.h>
#include <threads.h>
#include <stdatomic.h>

#define MAX_ARRAY_SIZE 10000
#define MAX_THREADS 256

typedef struct {
    int *array;
    size_t size;
    size_t max_threads;
    atomic_size_t active_threads;
    atomic_size_t phase;
    atomic_bool sorted;
} SortContext;

typedef struct {
    SortContext *ctx;
    size_t start_index;
    size_t end_index;
} ThreadData;

static void swap(int *a, int *b) {
    int temp = *a;
    *a = *b;
    *b = temp;
}

static int compare_and_swap(int *array, size_t i, size_t j) {
    if (i >= j || j >= MAX_ARRAY_SIZE) return 0;
    if (array[i] > array[j]) {
        swap(&array[i], &array[j]);
        return 1;
    }
    return 0;
}

static int worker_thread(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    SortContext *ctx = data->ctx;
    
    size_t last_phase = (size_t)-1;
    
    while (!atomic_load(&ctx->sorted)) {
        size_t current_phase = atomic_load(&ctx->phase);
        
        if (current_phase != last_phase && current_phase < ctx->size) {
            last_phase = current_phase;
            atomic_fetch_add(&ctx->active_threads, 1);
            
            size_t start = data->start_index;
            size_t end = data->end_index;
            
            if (current_phase % 2 == 0) {
                size_t i = (start % 2 == 0) ? start : start + 1;
                for (; i < end && i + 1 < ctx->size; i += 2) {
                    compare_and_swap(ctx->array, i, i + 1);
                }
            } else {
                size_t i = (start % 2 == 1) ? start : start + 1;
                if (i == 0) i = 1;
                for (; i < end && i + 1 < ctx->size; i += 2) {
                    compare_and_swap(ctx->array, i, i + 1);
                }
            }
            
            atomic_fetch_sub(&ctx->active_threads, 1);
        }
        
        thrd_yield();
    }
    
    return 0;
}

static int is_sorted(int *array, size_t size) {
    for (size_t i = 0; i + 1 < size; i++) {
        if (array[i] > array[i + 1]) {
            return 0;
        }
    }
    return 1;
}

static void batcher_sort_parallel(int *array, size_t size, size_t max_threads) {
    if (size <= 1) return;
    
    SortContext ctx;
    ctx.array = array;
    ctx.size = size;
    ctx.max_threads = max_threads;
    atomic_init(&ctx.active_threads, 0);
    atomic_init(&ctx.phase, 0);
    atomic_init(&ctx.sorted, 0);
    
    size_t threads_to_create = max_threads;
    if (threads_to_create > size / 2) {
        threads_to_create = size / 2;
    }
    if (threads_to_create == 0) {
        threads_to_create = 1;
    }
    if (threads_to_create > MAX_THREADS) {
        threads_to_create = MAX_THREADS;
    }
    
    thrd_t threads[MAX_THREADS];
    ThreadData thread_data[MAX_THREADS];
    
    size_t elements_per_thread = size / threads_to_create;
    if (elements_per_thread == 0) elements_per_thread = 1;
    
    for (size_t i = 0; i < threads_to_create; i++) {
        thread_data[i].ctx = &ctx;
        thread_data[i].start_index = i * elements_per_thread;
        thread_data[i].end_index = (i == threads_to_create - 1) ? size : (i + 1) * elements_per_thread;
        
        if (thrd_create(&threads[i], worker_thread, &thread_data[i]) != thrd_success) {
            fprintf(stderr, "Error: failed to create thread %zu\n", i);
            atomic_store(&ctx.sorted, 1);
            for (size_t j = 0; j < i; j++) {
                thrd_join(threads[j], NULL);
            }
            return;
        }
    }
    
    size_t max_phases = size;
    for (size_t phase = 0; phase < max_phases; phase++) {
        atomic_store(&ctx.phase, phase);
        
        while (atomic_load(&ctx.active_threads) < threads_to_create) {
            thrd_yield();
        }
        
        while (atomic_load(&ctx.active_threads) > 0) {
            thrd_yield();
        }
        
        if (is_sorted(array, size)) {
            atomic_store(&ctx.sorted, 1);
            break;
        }
    }
    
    atomic_store(&ctx.sorted, 1);
    
    for (size_t i = 0; i < threads_to_create; i++) {
        thrd_join(threads[i], NULL);
    }
}

static void batcher_sort_sequential(int *array, size_t size) {
    if (size <= 1) return;
    
    int sorted = 0;
    size_t max_phases = size;
    
    for (size_t phase = 0; phase < max_phases && !sorted; phase++) {
        sorted = 1;
        
        if (phase % 2 == 0) {
            for (size_t i = 0; i + 1 < size; i += 2) {
                if (compare_and_swap(array, i, i + 1)) {
                    sorted = 0;
                }
            }
        } else {
            for (size_t i = 1; i + 1 < size; i += 2) {
                if (compare_and_swap(array, i, i + 1)) {
                    sorted = 0;
                }
            }
        }
    }
}

static void print_array(int *array, size_t size) {
    for (size_t i = 0; i < size; i++) {
        printf("%d", array[i]);
        if (i + 1 < size) {
            printf(" ");
        }
    }
    printf("\n");
}

static int parse_unsigned(const char *str, size_t *result) {
    *result = 0;
    while (*str != '\0') {
        if (*str < '0' || *str > '9') {
            return 0;
        }
        size_t new_value = *result * 10 + (*str - '0');
        if (new_value < *result) {
            return 0;
        }
        *result = new_value;
        str++;
    }
    return *result > 0;
}

static int parse_int(const char *str, int *result) {
    int sign = 1;
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    
    *result = 0;
    int has_digit = 0;
    while (*str != '\0') {
        if (*str < '0' || *str > '9') {
            return 0;
        }
        has_digit = 1;
        int digit = *str - '0';
        int new_value = *result * 10 + digit;
        if ((new_value > 0 && *result < 0) || (new_value < 0 && *result > 0)) {
            return 0;
        }
        *result = new_value;
        str++;
    }
    if (!has_digit) return 0;
    *result *= sign;
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <max_threads> <array_size> [elements...]\n", argv[0]);
        fprintf(stderr, "  max_threads: maximum number of threads (1 for sequential)\n");
        fprintf(stderr, "  array_size: number of elements in array (max %d)\n", MAX_ARRAY_SIZE);
        fprintf(stderr, "  elements: optional list of integers (if not provided, random values will be used)\n");
        return 1;
    }
    
    size_t max_threads;
    if (!parse_unsigned(argv[1], &max_threads) || max_threads == 0) {
        fprintf(stderr, "Error: invalid max_threads value\n");
        return 1;
    }
    
    size_t array_size;
    if (!parse_unsigned(argv[2], &array_size) || array_size == 0) {
        fprintf(stderr, "Error: invalid array_size value\n");
        return 1;
    }
    
    if (array_size > MAX_ARRAY_SIZE) {
        fprintf(stderr, "Error: array_size exceeds maximum of %d\n", MAX_ARRAY_SIZE);
        return 1;
    }
    
    int array[MAX_ARRAY_SIZE];
    
    if (argc >= 3 + (int)array_size) {
        for (size_t i = 0; i < array_size; i++) {
            if (!parse_int(argv[3 + i], &array[i])) {
                fprintf(stderr, "Error: invalid integer at position %zu\n", i);
                return 1;
            }
        }
    } else {
        int seed = 42;
        for (size_t i = 0; i < array_size; i++) {
            seed = seed * 1103515245 + 12345;
            if (seed < 0) seed = -seed;
            array[i] = (seed / 65536) % 1000;
        }
    }
    
    printf("Original array: ");
    print_array(array, array_size);
    
    if (max_threads == 1) {
        printf("Using sequential sort\n");
        batcher_sort_sequential(array, array_size);
    } else {
        printf("Using parallel sort with max %zu threads\n", max_threads);
        batcher_sort_parallel(array, array_size, max_threads);
    }
    
    printf("Sorted array: ");
    print_array(array, array_size);
    
    if (!is_sorted(array, array_size)) {
        fprintf(stderr, "Error: array is not sorted correctly\n");
        return 1;
    }
    
    printf("Sort completed successfully\n");
    return 0;
}
