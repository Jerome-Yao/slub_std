#include "slub.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

#include "list.h"

namespace slub {
    void *Buddy::alloc_pages(size_t pages) {
        const size_t bytes = pages * PAGE_SIZE;
        void *ptr          = std::aligned_alloc(bytes, bytes);
        if (!ptr)
            return nullptr;
        std::memset(ptr, 0, bytes);
        return ptr;
    }

    // Correct implementation mock for free_pages
    void Buddy::free_pages(void *ptr, size_t) {
        std::free(ptr);
    }

    // ... (rest of methods)

    SlabHeader *slab_of(void *p) {
        auto ptr  = reinterpret_cast<uintptr_t>(p);
        auto base = align_down(ptr, SLAB_BYTES);
        return reinterpret_cast<SlabHeader *>(base);
    }

    void SlubCache::init_slab_headers(SlabHeader *slab) {
        auto base = reinterpret_cast<uintptr_t>(slab);
        auto cur  = base + sizeof(SlabHeader);
        cur       = align_up(cur, obj_align);
        auto end  = base + slab_bytes;

        size_t total = 0;
        auto p       = cur;
        while (p + obj_size <= end) {
            total++;
            p += obj_size;
        }

        slab->total = total;
        slab->inuse = 0;

        void *head = nullptr;
        // Construct freelist in reverse, so first alloc gets start of buffer
        // (optional but nice) Or forward. Current code loops 0 to total. i=0 is
        // first object.
        for (size_t i = 0; i < total; i++) {
            void *obj = reinterpret_cast<void *>(cur + i * obj_size);
            *reinterpret_cast<void **>(obj) = head;
            head                            = obj;
        }
        slab->freelist    = head;
        slab->owner_cache = this;
    }

    SlabHeader *SlubCache::new_slab() {
        void *mem        = Buddy::alloc_pages(pages);
        SlabHeader *slab = new (mem) SlabHeader{};
        init_slab_headers(slab);
        return slab;
    }

    void SlubCache::to_empty(SlabHeader *slab) {
        if (slab->state == SlabHeader::SlabState::PARTIAL) {
            partial.erase(decltype(partial)::iterator(slab));
        } else if (slab->state == SlabHeader::SlabState::FULL) {
            full.erase(decltype(full)::iterator(slab));
        }
        slab->state = SlabHeader::SlabState::EMPTY;
        empty.push_back(*slab);
    }

    void SlubCache::to_partial(SlabHeader *slab) {
        if (slab->state == SlabHeader::SlabState::EMPTY) {
            empty.erase(decltype(empty)::iterator(slab));
        } else if (slab->state == SlabHeader::SlabState::FULL) {
            full.erase(decltype(full)::iterator(slab));
        }
        slab->state = SlabHeader::SlabState::PARTIAL;
        partial.push_back(*slab);
    }

    // to_full remains mostly same but for consistency safety
    void SlubCache::to_full(SlabHeader *slab) {
        if (slab->state == SlabHeader::SlabState::PARTIAL) {
            partial.erase(decltype(partial)::iterator(slab));
        } else if (slab->state == SlabHeader::SlabState::EMPTY) {
            // Should not happen for to_full usually
            empty.erase(decltype(empty)::iterator(slab));
        }
        slab->state = SlabHeader::SlabState::FULL;
        full.push_back(*slab);
    }

    void *SlubCache::alloc() {
        SlabHeader *slab = nullptr;
        if (!partial.empty()) {
            slab = &partial.back();
        } else if (!empty.empty()) {
            slab = &empty.back();
            assert(slab);
            to_partial(slab);
        } else {
            slab = new_slab();
            assert(slab);
            slab->state = SlabHeader::SlabState::PARTIAL;
            partial.push_back(*slab);
        }

        assert(slab != nullptr);
        assert(slab->freelist != nullptr);
        void *obj      = slab->freelist;
        slab->freelist = *reinterpret_cast<void **>(obj);
        slab->inuse++;

        if (slab->inuse == slab->total) {
            to_full(slab);
        }
        return obj;
    }

    void SlubCache::free(void *p) {
        if (!p) {
            printf("can't free null pointer\n");
            return;
        }
        auto slab_header              = slab_of(p);
        *reinterpret_cast<void **>(p) = slab_header->freelist;
        slab_header->freelist         = p;
        slab_header->inuse--;
        if (slab_header->inuse == 0) {
            to_empty(slab_header);
        } else if (slab_header->inuse == slab_header->total - 1) {
            to_partial(slab_header);
        }
    }

    void *SlubAllocator::alloc(size_t n) {
        if (n > kMax) {
            size_t overhead = sizeof(BigAllocatorHeader) + ALIGN - 1;
            uint32_t pages  = (n + overhead + PAGE_SIZE - 1) / PAGE_SIZE;

            void *base = Buddy::alloc_pages(pages);
            void *user_addr =
                reinterpret_cast<void *>(align_up(reinterpret_cast<uintptr_t>(base + sizeof(BigAllocatorHeader)), ALIGN));

            void *hdr_addr = user_addr - sizeof(BigAllocatorHeader);

            auto *hdr     = reinterpret_cast<BigAllocatorHeader *>(hdr_addr);
            hdr           = reinterpret_cast<BigAllocatorHeader *>(hdr_addr);
            hdr->magic    = MAGIC;
            hdr->pages    = pages;
            hdr->raw_base = base;

            std::cout << "Allocating big block of size " << pages << std::endl;
            return reinterpret_cast<void *>(user_addr);
        }
        return caches[class_index(n)].alloc();
    }

    void SlubAllocator::free(void *ptr) {
        if (!ptr) {
            printf("can't free nullptr\n");
            return;
        }
        auto *big_header = reinterpret_cast<BigAllocatorHeader *>(
            ptr - sizeof(BigAllocatorHeader));
        if (big_header->magic == MAGIC) {
            assert(big_header->raw_base != nullptr);
            assert(big_header->pages > 0);
            std::cout << "Freeing big block of size " << big_header->pages << std::endl;
            Buddy::free_pages(big_header->raw_base, big_header->pages);
            return;
        }

        SlabHeader *slab_header = slab_of(ptr);
        assert(slab_header);
        SlubCache *cache        = slab_header->owner_cache;
        assert(cache);
        cache->free(ptr);
    }

    void SlubAllocator::free(void *ptr, size_t size) {
        if (!ptr) {
            printf("can't free nullptr\n");
            return;
        }
        if (size > kMax) {
            Buddy::free_pages(ptr, size);
            return;
        }
        caches[class_index(size)].free(ptr);
    }

    SlubAllocator::SlubAllocator()
        : caches{
              SlubCache(8, 8),     SlubCache(16, 16),     SlubCache(32, 32),
              SlubCache(64, 64),   SlubCache(128, 128),   SlubCache(256, 256),
              SlubCache(512, 512), SlubCache(1024, 1024), SlubCache(2048, 2048),
          }, slab_bytes(SLAB_BYTES) {}
}  // namespace slub
#include <iostream>
#include <vector>

// ...

int main() {
    using namespace slub;

    std::cout << "[Test 1] Basic Alignment Check" << std::endl;
    // Verify our assumptions about alignment helper
    assert(align_up(1, 8) == 8);
    assert(align_up(8, 8) == 8);
    assert(align_up(9, 8) == 16);
    assert(align_down(16, 4096) == 0);
    assert(align_down(4096 + 100, 4096) == 4096);
    std::cout << "  Passed." << std::endl;

    std::cout << "[Test 2] SlubCache Basic Alloc/Free" << std::endl;
    {
        // Object size 32, aligned 8. Page per slab 1 (4096 bytes).
        SlubCache cache(32, 8);
        void *p1 = cache.alloc();
        assert(p1 != nullptr);
        // Check alignment
        assert((uintptr_t)p1 % 8 == 0);

        void *p2 = cache.alloc();
        assert(p2 != nullptr);
        assert(p1 != p2);

        cache.free(p1);
        cache.free(p2);
    }
    std::cout << "  Passed." << std::endl;

    std::cout << "[Test 3] SlubCache Capacity & Refill" << std::endl;
    {
        // Use a large object size to have few objects per slab
        // Slab size = 4096. Header ~ 64 bytes.
        // Available ~ 4032.
        // If obj_size = 2048. 1 object per slab.
        SlubCache cache(2048, 8);

        std::vector<void *> ptrs;
        // Alloc 1 (Slab 1 created. InUse=1. Total=1. Full)
        ptrs.push_back(cache.alloc());
        SlabHeader *slab1 = slab_of(ptrs[0]);
        assert(slab1->state == SlabHeader::SlabState::FULL);

        // Alloc 2 (Slab 2 created. InUse=1. Total=1. Full)
        ptrs.push_back(cache.alloc());
        SlabHeader *slab2 = slab_of(ptrs[1]);
        assert(slab2->state == SlabHeader::SlabState::FULL);

        assert(ptrs[0] != ptrs[1]);
        assert(slab1 != slab2);

        // Free 1. Slab 1 becomes Empty (InUse -> 0).
        cache.free(ptrs[0]);
        assert(slab1->state == SlabHeader::SlabState::EMPTY);

        // Alloc 3. Should reuse Slab 1? Or Slab 2 if it had space (it doesn't).
        // Since Slab 1 is in Empty list, and Alloc prefers Partial, then Empty.
        // Checks Partial (empty) -> Checks Empty (has Slab 1) -> Reuse Slab 1.
        void *p3 = cache.alloc();
        // Since Slab 1 has only 1 object, p3 should be same address as ptrs[0]
        // effectively (same slot). Though pointer equality isn't strictly
        // guaranteed if freelist order changed (LIFO), but here only 1 slot.
        assert(p3 == ptrs[0]);
        // Slab 1 should be FULL again (1/1 used)
        assert(slab1->state == SlabHeader::SlabState::FULL);

        cache.free(p3);
        cache.free(ptrs[1]);

        assert(slab1->state == SlabHeader::SlabState::EMPTY);
        assert(slab2->state == SlabHeader::SlabState::EMPTY);
    }
    std::cout << "  Passed." << std::endl;

    std::cout << "[Test 4] SlubAllocator free(ptr) Integration" << std::endl;
    {
        SlubAllocator allocator;

        // Small-object size classes via SlubCache path.
        void *p1 = allocator.alloc(8);
        void *p2 = allocator.alloc(24);
        void *p3 = allocator.alloc(64);
        void *p4 = allocator.alloc(1024);
        void *p5 = allocator.alloc(2048);

        assert(p1 != nullptr);
        assert(p2 != nullptr);
        assert(p3 != nullptr);
        assert(p4 != nullptr);
        assert(p5 != nullptr);

        std::memset(p1, 0x11, 8);
        std::memset(p2, 0x22, 24);
        std::memset(p3, 0x33, 64);
        std::memset(p4, 0x44, 1024);
        std::memset(p5, 0x55, 2048);

        allocator.free(p1);
        allocator.free(p2);
        allocator.free(p3);
        allocator.free(p4);
        allocator.free(p5);

        // NOTE: free(ptr) for large allocations (> kMax) is not implemented
        // yet. Keep large-object free on the size-aware API for now.
        void *big = allocator.alloc(4096);
        assert(big != nullptr);
        std::memset(big, 0xAB, 4096);
        allocator.free(big);
    }
    std::cout << "  Passed." << std::endl;

    std::cout << "[Test 5] Stress Test" << std::endl;
    {
        SlubCache cache(64, 8);  // 64 bytes objects
        std::cout << "object num per slab in cache: " << cache.obj_per_slab()
                  << std::endl;
        std::vector<void *> ptrs;
        std::mt19937 gen(12345);
        std::uniform_int_distribution<> op_dist(
            0, 10);  // 0-6: alloc, 7-10: free bias

        for (int i = 0; i < 50000; ++i) {
            int op = op_dist(gen);
            if (op < 5 || ptrs.empty()) {
                // Alloc
                void *p = cache.alloc();
                assert(p != nullptr);

                SlabHeader *slab = slab_of(p);

                // Write pattern to ensure no overlap / corruption
                std::memset(p, 0xAA, 64);

                ptrs.push_back(p);
            } else {
                // Free
                std::uniform_int_distribution<> index_dist(0, ptrs.size() - 1);
                int idx = index_dist(gen);
                void *p = ptrs[idx];

                // Read check could be done here if we stored expected pattern

                cache.free(p);

                // Swap remove
                ptrs[idx] = ptrs.back();
                ptrs.pop_back();
            }

            // cache.debug_print();
        }

        // Clean up remaining
        for (void *p : ptrs) {
            cache.free(p);
        }
    }
    std::cout << "  Passed." << std::endl;

    std::cout << "All tests passed successfully!" << std::endl;
    return 0;
}
