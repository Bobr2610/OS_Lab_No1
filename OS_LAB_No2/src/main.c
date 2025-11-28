#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>

#define BUF_SIZE 256

/* Вспомогательные функции для вывода */
static void print_stdout(const char *str) {
    write(STDOUT_FILENO, str, strlen(str));
}

static void print_stderr(const char *str) {
    write(STDERR_FILENO, str, strlen(str));
}

/* Структуры данных для хранения данных о сортировке */
typedef struct {
    int *array;
    int n;
    int max_threads;
    int active_threads;
    pthread_mutex_t *mutex;
} sort_data_t;
/* Структура для хранения данных о потоке */
typedef struct {
    sort_data_t *data;
    int start;
    int end;
    int step;
} thread_data_t;
/* Функция для сравнения и обмена значениями */
static void compare_swap(int *a, int *b) {
    if (*a > *b) {
        int temp = *a;
        *a = *b;
        *b = temp;
    }
}
/* Функция для слияния двух подмассивов */
static void *batcher_merge_thread(void *arg) {
    thread_data_t *tdata = (thread_data_t *)arg;
    sort_data_t *data = tdata->data;
    /* Блокировка мьютекса для активных потоков */
    pthread_mutex_lock(data->mutex);
    data->active_threads++;
    pthread_mutex_unlock(data->mutex);
    /* Слияние двух подмассивов */
    for (int i = tdata->start; i < tdata->end; i += tdata->step) {
        if (i + tdata->step / 2 < data->n) {
            compare_swap(&data->array[i], &data->array[i + tdata->step / 2]);
        }
    }
    /* Разблокировка мьютекса для активных потоков */
    pthread_mutex_lock(data->mutex);
    data->active_threads--;
    pthread_mutex_unlock(data->mutex);
    /* Возвращение NULL */
    return NULL;
}
/* Функция для слияния двух подмассивов */
static void batcher_merge(int *array, int n, int step, sort_data_t *data) {
    int num_operations = 0;
    for (int i = 0; i < n - step / 2; i += step) {
        num_operations++;
    }
    /* Если количество операций равно 0, то выход */
    if (num_operations == 0) return;
    /* Определение количества потоков для использования */
    int threads_to_use = data->max_threads;
    if (threads_to_use > num_operations) {
        threads_to_use = num_operations;
    }
    /* Если количество потоков равно 1, то выполняем слияние без использования потоков */
    if (threads_to_use <= 1) {
        for (int i = 0; i < n - step / 2; i += step) {
            if (i + step / 2 < n) {
                compare_swap(&array[i], &array[i + step / 2]);
            }
        }
        return;
    }
    /* Определение количества операций на один поток */
    int operations_per_thread = (num_operations + threads_to_use - 1) / threads_to_use;
    /* Выделение памяти для потоков */
    pthread_t *threads = (pthread_t *)malloc(threads_to_use * sizeof(pthread_t));
    /* Выделение памяти для данных о потоке */
    thread_data_t *tdata_array = (thread_data_t *)malloc(threads_to_use * sizeof(thread_data_t));
    
    if (!threads || !tdata_array) {
        print_stderr("Error: Memory allocation failed\n");
        free(threads);
        free(tdata_array);
        return;
    }
    
    int thread_count = 0;

    for (int t = 0; t < threads_to_use; t++) {
        int start = t * operations_per_thread * step;
        int end = (t + 1) * operations_per_thread * step;
        if (end > n) end = n;
        /* Если начало больше или равно концу, то выход */
        if (start >= n - step / 2) break;
        /* Заполнение данных для потока */
        tdata_array[thread_count].data = data;
        tdata_array[thread_count].start = start;
        tdata_array[thread_count].end = end;
        tdata_array[thread_count].step = step;
        /* Создание потока*/
        if (pthread_create(&threads[thread_count], NULL, batcher_merge_thread, &tdata_array[thread_count]) == 0) {
            thread_count++;
        }
    }
    /* Ожидание завершения всех потоков */
    for (int i = 0; i < thread_count; i++) {
        pthread_join(threads[i], NULL);
    }
    /* Освобождение памяти */
    free(threads);
    free(tdata_array);
}
/* Функция для четно-нечетной сортировки Бетчера */
static void batcher_odd_even_sort(int *array, int n, int max_threads) {
    if (n <= 1) return;
    /* Инициализация мьютекса */
    pthread_mutex_t mutex;
    /* Если не удалось инициализировать мьютекс, то выход */
    if (pthread_mutex_init(&mutex, NULL) != 0) {
        print_stderr("Error: Failed to initialize mutex\n");
        return;
    }
    /* Заполнение данных для сортировки */
    sort_data_t data = {
        .array = array,
        .n = n,
        .max_threads = max_threads,
        .active_threads = 0,
        .mutex = &mutex
    };
    
    /* Цикл для выполнения четно-нечетной сортировки Бетчера */
    for (int step = 2; step <= n; step *= 2) {
        /* Цикл для выполнения слияния */
        for (int substep = step; substep >= 2; substep /= 2) {
            batcher_merge(array, n, substep, &data);
        }
    }
    
    pthread_mutex_destroy(&mutex);
}

static void print_array(int *array, int n) {
    char buf[BUF_SIZE];
    for (int i = 0; i < n; i++) {
        snprintf(buf, BUF_SIZE, "%d ", array[i]);
        print_stdout(buf);
    }
    print_stdout("\n");
}
/* Функция для проверки, является ли массив отсортированным */
static bool is_sorted(int *array, int n) {
    for (int i = 1; i < n; i++) {
        if (array[i] < array[i - 1]) {
            return false;
        }
    }
    return true;
}
/* Функция для вывода массива */
int main(int argc, char *argv[]) {
    char buf[BUF_SIZE];
    
    if (argc < 3) {
        snprintf(buf, BUF_SIZE, "Usage: %s <max_threads> <array_size> [seed]\n", argv[0]);
        print_stderr(buf);
        snprintf(buf, BUF_SIZE, "Example: %s 4 1000\n", argv[0]);
        print_stderr(buf);
        return EXIT_FAILURE;
    }
    /* Преобразование строки в целое число */
    /* atoi - функция для преобразования строки в целое число */
    int max_threads = atoi(argv[1]);
    int array_size = atoi(argv[2]);
    int seed = (argc > 3) ? atoi(argv[3]) : (int)time(NULL);
    
    if (max_threads < 1) {
        print_stderr("Error: max_threads must be at least 1\n");
        return EXIT_FAILURE;
    }
    
    if (array_size < 1) {
        print_stderr("Error: array_size must be at least 1\n");
        return EXIT_FAILURE;
    }
    
    int *array = (int *)malloc(array_size * sizeof(int));
    if (!array) {
        print_stderr("Error: Memory allocation failed\n");
        return EXIT_FAILURE;
    }
    /* Генерация массива */
    srand(seed);
    snprintf(buf, BUF_SIZE, "Generating array of size %d with seed %d\n", array_size, seed);
    print_stdout(buf);
    for (int i = 0; i < array_size; i++) {
        array[i] = rand() % 10000;
    }
    
    print_stdout("Original array (first 20 elements): ");
    print_array(array, array_size < 20 ? array_size : 20);
    
    clock_t start = clock();
    batcher_odd_even_sort(array, array_size, max_threads);
    clock_t end = clock();
    
    double time_taken = ((double)(end - start)) / CLOCKS_PER_SEC;
    
    print_stdout("Sorted array (first 20 elements): ");
    print_array(array, array_size < 20 ? array_size : 20);
    
    bool sorted = is_sorted(array, array_size);
    snprintf(buf, BUF_SIZE, "Array is %s\n", sorted ? "sorted correctly" : "NOT sorted correctly");
    print_stdout(buf);
    snprintf(buf, BUF_SIZE, "Time taken: %.6f seconds\n", time_taken);
    print_stdout(buf);
    snprintf(buf, BUF_SIZE, "Max threads used: %d\n", max_threads);
    print_stdout(buf);
    print_stdout("\nTo verify thread count, use:\n");
    snprintf(buf, BUF_SIZE, "  ps -eLf | grep %s | wc -l\n", argv[0]);
    print_stdout(buf);
    snprintf(buf, BUF_SIZE, "  top -H -p $(pgrep -f %s)\n", argv[0]);
    print_stdout(buf);
    
    free(array);
    return sorted ? EXIT_SUCCESS : EXIT_FAILURE;
}
