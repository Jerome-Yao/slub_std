#include "slub.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <chrono>


namespace slub {
    static size_t g_total_pages   = 0;
    static size_t g_current_pages = 0;
    static double g_buddy_alloc_time_ms = 0;
    static double g_buddy_free_time_ms = 0;

    void *Buddy::alloc_pages(size_t pages) {
        auto start = std::chrono::high_resolution_clock::now();
        const size_t bytes = pages * PAGE_SIZE;
        void *ptr          = std::aligned_alloc(PAGE_SIZE, bytes);
        if (!ptr)
            return nullptr;
        std::memset(ptr, 0, bytes);

        g_total_pages += pages;
        g_current_pages += pages;
        auto end = std::chrono::high_resolution_clock::now();
        g_buddy_alloc_time_ms += std::chrono::duration<double, std::milli>(end - start).count();

        return ptr;
    }

    void Buddy::free_pages(void *ptr, size_t pages) {
        if (ptr) {
            auto start = std::chrono::high_resolution_clock::now();
            std::free(ptr);
            g_current_pages -= pages;
            auto end = std::chrono::high_resolution_clock::now();
            g_buddy_free_time_ms += std::chrono::duration<double, std::milli>(end - start).count();
        }
    }

    size_t Buddy::get_current_pages() {
        return g_current_pages;
    }

    size_t Buddy::get_total_allocated_pages() {
        return g_total_pages;
    }

    double Buddy::get_alloc_time_ms() {
        return g_buddy_alloc_time_ms;
    }

    double Buddy::get_free_time_ms() {
        return g_buddy_free_time_ms;
    }

    void Buddy::reset_timers() {
        g_buddy_alloc_time_ms = 0;
        g_buddy_free_time_ms = 0;
    }
}  // namespace slub
