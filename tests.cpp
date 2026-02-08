#include "slub.h"
#include <iostream>
#include <vector>
#include <cassert>
#include <cstring>
#include <random>

int main() {
    using namespace slub;

    struct SmallObj {
        int a;
        float b;
    };

    struct TinyObj {
        std::uint8_t x;
    };

    struct BigObj {
        std::byte payload[3000];
    };

    std::cout << "[Test 1] Basic Alignment Helpers" << std::endl;
    assert(align_up(1, 8) == 8);
    assert(align_up(8, 8) == 8);
    assert(align_up(9, 8) == 16);
    assert(align_down(16, 4096) == 0);
    assert(align_down(4096 + 100, 4096) == 4096);
    std::cout << "  Passed." << std::endl;

    std::cout << "[Test 2] Template Small Type Alloc/Free" << std::endl;
    {
        SlubAllocator<SmallObj> alloc;
        void *p1 = alloc.alloc();
        void *p2 = alloc.alloc();
        assert(p1 != nullptr);
        assert(p2 != nullptr);
        assert(p1 != p2);
        assert(reinterpret_cast<uintptr_t>(p1) % std::max(alignof(SmallObj), alignof(void *)) == 0);
        assert(reinterpret_cast<uintptr_t>(p2) % std::max(alignof(SmallObj), alignof(void *)) == 0);
        alloc.free(p1);
        alloc.free(p2);
    }
    std::cout << "  Passed." << std::endl;

    std::cout << "[Test 3] Tiny Type (Freelist Pointer Fit)" << std::endl;
    {
        SlubAllocator<TinyObj> alloc;
        std::vector<void *> ptrs;
        ptrs.reserve(128);

        for (int i = 0; i < 128; ++i) {
            void *p = alloc.alloc();
            assert(p != nullptr);
            assert(reinterpret_cast<uintptr_t>(p) % alignof(void *) == 0);
            ptrs.push_back(p);
        }

        for (void *p : ptrs) {
            alloc.free(p);
        }
    }
    std::cout << "  Passed." << std::endl;

    std::cout << "[Test 4] Big Type Path Alloc/Free" << std::endl;
    {
        SlubAllocator<BigObj> alloc;
        void *p = alloc.alloc();
        assert(p != nullptr);
        assert(reinterpret_cast<uintptr_t>(p) % PAGE_SIZE == 0);
        std::memset(p, 0xAB, sizeof(BigObj));
        alloc.free(p);
    }
    std::cout << "  Passed." << std::endl;

    std::cout << "[Test 5] Template Stress (Small Type)" << std::endl;
    {
        SlubAllocator<SmallObj> alloc;
        std::vector<void *> ptrs;
        std::mt19937 gen(12345);
        std::uniform_int_distribution<> op_dist(0, 10);

        for (int i = 0; i < 30000; ++i) {
            const int op = op_dist(gen);
            if (op < 5 || ptrs.empty()) {
                void *p = alloc.alloc();
                assert(p != nullptr);
                std::memset(p, 0xCD, sizeof(SmallObj));
                ptrs.push_back(p);
            } else {
                std::uniform_int_distribution<size_t> idx_dist(0, ptrs.size() - 1);
                size_t idx = idx_dist(gen);
                void *p    = ptrs[idx];
                alloc.free(p);
                ptrs[idx] = ptrs.back();
                ptrs.pop_back();
            }
        }

        for (void *p : ptrs) {
            alloc.free(p);
        }
    }
    std::cout << "  Passed." << std::endl;

    std::cout << "All tests passed successfully!" << std::endl;
    return 0;
}
