#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

#include "slub.h"

using namespace slub;

double g_now_overhead_ms = 0;

void calibrate_timer() {
    const int iterations = 1000000;
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        auto t1 = std::chrono::high_resolution_clock::now();
        t1 = std::chrono::high_resolution_clock::now();
        t1 = std::chrono::high_resolution_clock::now();
        t1 = std::chrono::high_resolution_clock::now();
        t1 = std::chrono::high_resolution_clock::now();
        t1 = std::chrono::high_resolution_clock::now();
        t1 = std::chrono::high_resolution_clock::now();
        t1 = std::chrono::high_resolution_clock::now();
        (void)t1;
    }
    auto end = std::chrono::high_resolution_clock::now();
    double g_now_overhead_8_ms = std::chrono::duration<double, std::milli>(end - start).count() / iterations;

    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        auto t1 = std::chrono::high_resolution_clock::now();
        t1 = std::chrono::high_resolution_clock::now();
        t1 = std::chrono::high_resolution_clock::now();
        t1 = std::chrono::high_resolution_clock::now();
        t1 = std::chrono::high_resolution_clock::now();
        t1 = std::chrono::high_resolution_clock::now();
        t1 = std::chrono::high_resolution_clock::now();
        t1 = std::chrono::high_resolution_clock::now();
        t1 = std::chrono::high_resolution_clock::now();
        t1 = std::chrono::high_resolution_clock::now();
        t1 = std::chrono::high_resolution_clock::now();
        t1 = std::chrono::high_resolution_clock::now();
        t1 = std::chrono::high_resolution_clock::now();
        t1 = std::chrono::high_resolution_clock::now();
        t1 = std::chrono::high_resolution_clock::now();
        t1 = std::chrono::high_resolution_clock::now();
        (void)t1;
    }
    end = std::chrono::high_resolution_clock::now();
    double g_now_overhead_16_ms = std::chrono::duration<double, std::milli>(end - start).count() / iterations;
    g_now_overhead_ms = (g_now_overhead_16_ms - g_now_overhead_8_ms) / 8.0;
}

void print_buddy_stats() {
    std::cout << "[Buddy Status] " << "Current: " << Buddy::get_current_pages()
              << " pages (" << Buddy::get_current_pages() * PAGE_SIZE / 1024
              << " KB), "
              << "Total Ever: " << Buddy::get_total_allocated_pages()
              << " pages" << std::endl;
}

void print_metric(const std::string& label, const std::vector<double>& values, const std::string& unit) {
    auto [min_it, max_it] = std::minmax_element(values.begin(), values.end());
    double sum = 0;
    for (double v : values) sum += v;
    double avg = sum / values.size();

    double variance_sum = 0;
    for (double v : values) variance_sum += (v - avg) * (v - avg);
    double variance = variance_sum / values.size();
    
    std::cout << "  - " << std::left << std::setw(25) << label 
              << ": [" << std::fixed << std::setprecision(3) << *min_it 
              << " / " << *max_it << "] " << unit 
              << " (avg: " << avg << ", var: " << variance << ")" << std::endl;
}

template <typename T>
void run_benchmark(const std::string& name, int iterations) {
    const int RUNS = 10;
    std::vector<double> alloc_times, free_times, pure_alloc_ns, pure_free_ns;
    
    std::cout << ">>> Running Benchmark: " << name << " (" << iterations
              << " iterations, " << RUNS << " runs)" << std::endl;

    SlubStats peak_stats;

    for (int r = 0; r < RUNS; ++r) {
        SlubAllocator<T> alloc;
        std::vector<void*> ptrs;
        ptrs.resize(iterations);

        // Alloc Phase
        Buddy::reset_timers();
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iterations; ++i) {
            ptrs[i] = alloc.alloc();
        }
        auto end = std::chrono::high_resolution_clock::now();
        
        double buddy_alloc_ms = Buddy::get_alloc_time_ms();
        size_t buddy_alloc_count = Buddy::get_alloc_count();
        double total_alloc_ms = std::chrono::duration<double, std::milli>(end - start).count();
        alloc_times.push_back(total_alloc_ms);
        
        double pure_alloc_ms = total_alloc_ms - buddy_alloc_ms - (buddy_alloc_count * g_now_overhead_ms);
        pure_alloc_ns.push_back((pure_alloc_ms * 1e6) / iterations);

        if (r == RUNS - 1) peak_stats = alloc.get_stats();

        // Free Phase
        Buddy::reset_timers();
        start = std::chrono::high_resolution_clock::now();
        for (void* p : ptrs) {
            alloc.free(p);
        }
        end = std::chrono::high_resolution_clock::now();
        
        double buddy_free_ms = Buddy::get_free_time_ms();
        size_t buddy_free_count = Buddy::get_free_count();
        double total_free_ms = std::chrono::duration<double, std::milli>(end - start).count();
        free_times.push_back(total_free_ms);
        
        double pure_free_ms = total_free_ms - buddy_free_ms - (buddy_free_count * g_now_overhead_ms);
        pure_free_ns.push_back((pure_free_ms * 1e6) / iterations);
    }

    print_metric("Total Alloc Time", alloc_times, "ms");
    print_metric("Pure SLUB Alloc", pure_alloc_ns, "ns/op");
    print_metric("Total Free Time", free_times, "ms");
    print_metric("Pure SLUB Free", pure_free_ns, "ns/op");
    
    std::cout << "  - Peak Slub Memory       : " << peak_stats.memory_usage_bytes / 1024 << " KB (" << peak_stats.total_slabs << " slabs)" << std::endl;
    std::cout << "  - Object Utilization     : " << peak_stats.objects_inuse << " / " << peak_stats.objects_total 
              << " (" << (peak_stats.objects_total > 0 ? (double)peak_stats.objects_inuse / peak_stats.objects_total * 100.0 : 0) << "%)" << std::endl;
    std::cout << std::endl;
}

struct Small {
    char data[32];
};
struct Medium {
    char data[256];
};
struct Large {
    char data[1024];
};
struct Huge {
    char data[4096];
};  // Above kMax (2048)

int main() {
    calibrate_timer();

    std::cout << "=== SLUB Allocator Benchmark ===" << std::endl;
    std::cout << "Clock overhead: " << g_now_overhead_ms * 1e6 << " ns/call" << std::endl;
    print_buddy_stats();
    std::cout << std::endl;

    run_benchmark<Small>("Small (32B)", 500000);
    run_benchmark<Medium>("Medium (256B)", 100000);
    run_benchmark<Large>("Large (1kB)", 50000);
    run_benchmark<Huge>("Huge (4kB, Big Path)", 10000);

    std::cout << "Final Results:" << std::endl;
    print_buddy_stats();
    std::cout << "================================" << std::endl;

    return 0;
}
