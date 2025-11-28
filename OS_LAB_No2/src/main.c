#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>

typedef struct {
    int *array;
    int n;
    int max_threads;
    int active_threads;
    pthread_mutex_t *mutex;
    pthread_mutex_t *active_mutex;
} sort_data_t;

typedef struct {
    sort_data_t *data;
    int start;
    int end;
    int step;
} thread_data_t;

static void compare_swap(int *a, int *b) {
    if (*a > *b) {
        int temp = *a;
        *a = *b;
        *b = temp;
    }
}

static void *batcher_merge_thread(void *arg) {
    thread_data_t *tdata = (thread_data_t *)arg;
    sort_data_t *data = tdata->data;
    
    pthread_mutex_lock(data->active_mutex);
    data->active_threads++;
    pthread_mutex_unlock(data->active_mutex);
    
    for (int i = tdata->start; i < tdata->end; i += tdata->step) {
        if (i + tdata->step / 2 < data->n) {
            compare_swap(&data->array[i], &data->array[i + tdata->step / 2]);
        }
    }
    
    pthread_mutex_lock(data->active_mutex);
    data->active_threads--;
    pthread_mutex_unlock(data->active_mutex);
    
    return NULL;
}

static void batcher_merge(int *array, int n, int step, sort_data_t *data) {
    int num_operations = 0;
    for (int i = 0; i < n - step / 2; i += step) {
        num_operations++;
    }
    
    if (num_operations == 0) return;
    
    int threads_to_use = data->max_threads;
    if (threads_to_use > num_operations) {
        threads_to_use = num_operations;
    }
    
    if (threads_to_use <= 1) {
        for (int i = 0; i < n - step / 2; i += step) {
            if (i + step / 2 < n) {
                compare_swap(&array[i], &array[i + step / 2]);
            }
        }
        return;
    }
    
    int operations_per_thread = (num_operations + threads_to_use - 1) / threads_to_use;
    pthread_t *threads = (pthread_t *)malloc(threads_to_use * sizeof(pthread_t));
    thread_data_t *tdata_array = (thread_data_t *)malloc(threads_to_use * sizeof(thread_data_t));
    
    if (!threads || !tdata_array) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        free(threads);
        free(tdata_array);
        return;
    }
    
    int thread_count = 0;
    for (int t = 0; t < threads_to_use; t++) {
        int start = t * operations_per_thread * step;
        int end = (t + 1) * operations_per_thread * step;
        if (end > n) end = n;
        
        if (start >= n - step / 2) break;
        
        tdata_array[thread_count].data = data;
        tdata_array[thread_count].start = start;
        tdata_array[thread_count].end = end;
        tdata_array[thread_count].step = step;
        
        if (pthread_create(&threads[thread_count], NULL, batcher_merge_thread, &tdata_array[thread_count]) == 0) {
            thread_count++;
        }
    }
    
    for (int i = 0; i < thread_count; i++) {
        pthread_join(threads[i], NULL);
    }
    
    free(threads);
    free(tdata_array);
}

static void batcher_odd_even_sort(int *array, int n, int max_threads) {
    if (n <= 1) return;
    
    pthread_mutex_t mutex;
    pthread_mutex_t active_mutex;
    
    if (pthread_mutex_init(&mutex, NULL) != 0 || pthread_mutex_init(&active_mutex, NULL) != 0) {
        fprintf(stderr, "Error: Failed to initialize mutexes\n");
        return;
    }
    
    sort_data_t data = {
        .array = array,
        .n = n,
        .max_threads = max_threads,
        .active_threads = 0,
        .mutex = &mutex,
        .active_mutex = &active_mutex
    };
    
    // Batcher's odd-even sort: iterate through step sizes
    for (int step = 2; step <= n; step *= 2) {
        // For each step, perform odd-even merge
        for (int substep = step; substep >= 2; substep /= 2) {
            batcher_merge(array, n, substep, &data);
        }
    }
    
    pthread_mutex_destroy(&mutex);
    pthread_mutex_destroy(&active_mutex);
}

static void print_array(int *array, int n) {
    for (int i = 0; i < n; i++) {
        printf("%d ", array[i]);
    }
    printf("\n");
}

static bool is_sorted(int *array, int n) {
    for (int i = 1; i < n; i++) {
        if (array[i] < array[i - 1]) {
            return false;
        }
    }
    return true;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <max_threads> <array_size> [seed]\n", argv[0]);
        fprintf(stderr, "Example: %s 4 1000\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    int max_threads = atoi(argv[1]);
    int array_size = atoi(argv[2]);
    int seed = (argc > 3) ? atoi(argv[3]) : (int)time(NULL);
    
    if (max_threads < 1) {
        fprintf(stderr, "Error: max_threads must be at least 1\n");
        return EXIT_FAILURE;
    }
    
    if (array_size < 1) {
        fprintf(stderr, "Error: array_size must be at least 1\n");
        return EXIT_FAILURE;
    }
    
    int *array = (int *)malloc(array_size * sizeof(int));
    if (!array) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        return EXIT_FAILURE;
    }
    
    srand(seed);
    printf("Generating array of size %d with seed %d\n", array_size, seed);
    for (int i = 0; i < array_size; i++) {
        array[i] = rand() % 10000;
    }
    
    printf("Original array (first 20 elements): ");
    print_array(array, array_size < 20 ? array_size : 20);
    
    clock_t start = clock();
    batcher_odd_even_sort(array, array_size, max_threads);
    clock_t end = clock();
    
    double time_taken = ((double)(end - start)) / CLOCKS_PER_SEC;
    
    printf("Sorted array (first 20 elements): ");
    print_array(array, array_size < 20 ? array_size : 20);
    
    bool sorted = is_sorted(array, array_size);
    printf("Array is %s\n", sorted ? "sorted correctly" : "NOT sorted correctly");
    printf("Time taken: %.6f seconds\n", time_taken);
    printf("Max threads used: %d\n", max_threads);
    printf("\nTo verify thread count, use:\n");
    printf("  ps -eLf | grep %s | wc -l\n", argv[0]);
    printf("  top -H -p $(pgrep -f %s)\n", argv[0]);
    
    free(array);
    return sorted ? EXIT_SUCCESS : EXIT_FAILURE;
}
