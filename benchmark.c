#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include "alloc.h"
#include <stdatomic.h>


#define NUM_OPERATIONS 1000000
#define NUM_THREADS 2
#define NUM_RUNS 5

typedef struct {
    int thread_id;
    int num_ops;
    double elapsed_time;
} thread_data_t;

double benchmark_single_threaded_custom(int num_ops) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // unsigned int seed = 42;
    for (int i = 0; i < num_ops; i++) {
        int size = (i % 1000) + 1;
        void *p = alloc(size);
        if (p) dealloc(p);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed = (end.tv_sec - start.tv_sec) +
                     (end.tv_nsec - start.tv_nsec) / 1e9;
    return elapsed;
}


double benchmark_single_threaded_malloc(int num_ops) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    unsigned int seed = 42;
    for (int i = 0; i < num_ops; i++) {
        int size = (rand_r(&seed) % 2000) + 1;
        void *p = malloc(size);
        if (p) free(p);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
}
void *benchmark_worker_malloc(void *arg) {
    thread_data_t *data = (thread_data_t *)arg;
    
    unsigned int seed = time(NULL) + data->thread_id * 12345;
    for (int i = 0; i < data->num_ops; i++) {
        int size = (rand_r(&seed) % 2000) + 1;
        void *p = malloc(size);
        if (p) free(p);
    }
    
    return NULL;
}

void *benchmark_worker_custom(void *arg) {
    thread_data_t *data = (thread_data_t *)arg;
    // printf("Thread %d starting...\n", data->thread_id);
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // FIX 1: Use thread-local random state
    unsigned int seed = time(NULL) + data->thread_id * 12345;
    
    for (int i = 0; i < data->num_ops; i++) {
        // FIX 2: Random sizes from 1 to 2000 bytes (covers all 8 size classes)
        int size = (rand_r(&seed) % 2000) + 1;
        void *p = alloc(size);
        if (p) dealloc(p);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    data->elapsed_time = (end.tv_sec - start.tv_sec) +
                         (end.tv_nsec - start.tv_nsec) / 1e9;
    // printf("Thread %d finished in %.3f sec\n", data->thread_id, data->elapsed_time);
    return NULL;
}

double benchmark_multi_threaded_custom(int num_threads, int ops_per_thread) {
    pthread_t threads[num_threads];
    thread_data_t thread_data[num_threads];

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < num_threads; i++) {
        // printf("on tfhread %d\n", i);
        thread_data[i].thread_id = i;
        thread_data[i].num_ops = ops_per_thread;
        pthread_create(&threads[i], NULL, benchmark_worker_custom, &thread_data[i]);
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    return (end.tv_sec - start.tv_sec) +
           (end.tv_nsec - start.tv_nsec) / 1e9;
}

double benchmark_multi_threaded_malloc(int num_threads, int ops_per_thread) {
    pthread_t threads[num_threads];
    thread_data_t thread_data[num_threads];

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < num_threads; i++) {
        thread_data[i].thread_id = i;
        thread_data[i].num_ops = ops_per_thread;
        pthread_create(&threads[i], NULL, benchmark_worker_malloc, &thread_data[i]);
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
}


int main() {
    init_allocator();

    printf("=== Memory Allocator Benchmark ===\n");
    printf("Operations: %d | Runs: %d\n\n", NUM_OPERATIONS, NUM_RUNS);

    // Warmup
    benchmark_single_threaded_custom(1000);
    benchmark_single_threaded_malloc(1000);

    // Single-threaded benchmarks
    printf("--- Single-Threaded Performance ---\n");

    double custom_times[NUM_RUNS];
    double malloc_times[NUM_RUNS];

    for (int i = 0; i < NUM_RUNS; i++) {
        custom_times[i] = benchmark_single_threaded_custom(NUM_OPERATIONS);
        malloc_times[i] = benchmark_single_threaded_malloc(NUM_OPERATIONS);
    }

    double custom_avg = 0, malloc_avg = 0;
    for (int i = 0; i < NUM_RUNS; i++) {
        custom_avg += custom_times[i];
        malloc_avg += malloc_times[i];
    }
    custom_avg /= NUM_RUNS;
    malloc_avg /= NUM_RUNS;

    double custom_throughput = NUM_OPERATIONS / custom_avg;
    double malloc_throughput = NUM_OPERATIONS / malloc_avg;

    printf("Custom allocator: %.3f sec | %10.0f ops/sec\n", custom_avg, custom_throughput);
    printf("System malloc:    %.3f sec | %10.0f ops/sec\n", malloc_avg, malloc_throughput);
    printf("Speedup:          %.2fx %s\n\n",
           malloc_avg / custom_avg,
           custom_avg < malloc_avg ? "faster" : "slower");

    // Multi-threaded benchmarks
    printf("--- Multi-Threaded Performance ---\n");
    printf("%-15s %-15s %-15s %-15s %-10s\n",
           "Threads", "Custom (sec)", "Malloc (sec)", "Custom ops/s", "Speedup");
    printf("-----------------------------------------------------------------------\n");

    int thread_counts[] = {2, 4, 8};
    for (int t = 0; t < 3; t++) {
        int num_threads = thread_counts[t];
        int ops_per_thread = NUM_OPERATIONS / num_threads;

        double custom_time = 0, malloc_time = 0;
        for (int i = 0; i < NUM_RUNS; i++) {
            // printf("on run %d\n", i);
            custom_time += benchmark_multi_threaded_custom(num_threads, ops_per_thread);
            // printf("Heap expansions: %lu\n", atomic_load(&heap_expand_count));
            malloc_time += benchmark_multi_threaded_malloc(num_threads, ops_per_thread);
        }
        
        custom_time /= NUM_RUNS;
        malloc_time /= NUM_RUNS;

        double custom_ops_per_sec = (num_threads * ops_per_thread) / custom_time;

        printf("%-15d %-15.3f %-15.3f %-15.0f %.2fx %s\n",
               num_threads, custom_time, malloc_time, custom_ops_per_sec,
               malloc_time / custom_time,
               custom_time < malloc_time ? "faster" : "slower");
    }

    printf("\n=== Benchmark Complete ===\n");
    printf("\nSize class distribution:\n");

    return 0;
}
