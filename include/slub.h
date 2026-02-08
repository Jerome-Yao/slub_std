#pragma once

#include <list.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>

namespace slub {

    constexpr size_t PAGE_SIZE = 4096;
    constexpr size_t PAGES_PER_SLAB = 1;
    constexpr size_t SLAB_BYTES = PAGE_SIZE * PAGES_PER_SLAB;
    constexpr size_t ALIGN = 16;
    
    constexpr uint32_t MAGIC = 0x12345678;

    struct Buddy {
        static void *alloc_pages(size_t pages);
        static void free_pages(void *p, size_t pages);
    };

    static inline std::uintptr_t align_down(std::uintptr_t addr,
                                            std::uintptr_t align) {
        return addr & ~(align - 1);
    }
    static inline std::uintptr_t align_up(std::uintptr_t addr,
                                          std::uintptr_t align) {
        return (addr + align - 1) & ~(align - 1);
    }

    // define for SlubHeader
    class SlubCache;
    
    struct SlabHeader {
        enum class SlabState { EMPTY, PARTIAL, FULL };
        SlabHeader *prev{};
        SlabHeader *next{};
        void *freelist{};
        uint32_t inuse{};
        uint32_t total{};
        SlabState state{};
        SlubCache* owner_cache{};
        SlabHeader()
            : prev(nullptr),
              next(nullptr),
              freelist(nullptr),
              state(SlabState::EMPTY),
              inuse(0),
              total(0) {}
    };

    static_assert(util::IntrusiveListNodeTrait<SlabHeader>,
                  "SlabHeader fails to be a valid intrusive list node");

    static SlabHeader *slab_of(void *p);
    
    class SlubCache {
    public:
        SlubCache(size_t obj_size, size_t obj_align, size_t pages_per_slab = PAGES_PER_SLAB)
            : obj_size(std::max(obj_size, sizeof(void *))),
              obj_align(std::max(obj_align, alignof(void *))),
              pages(pages_per_slab),
              slab_bytes(SLAB_BYTES) {
            assert((slab_bytes & (slab_bytes - 1)) == 0 &&
                   "slab_bytes must be a power of 2");
        }
        void *alloc();
        void free(void *ptr);

        size_t object_size() const {
            return obj_size;
        }

        size_t size_of_partial() const {
            return partial.size();
        }
        size_t size_of_full() const {
            return full.size();
        }
        size_t size_of_empty() const {
            return empty.size();
        }
        size_t obj_per_slab() const {
            auto base = 0;
            auto cur  = base + sizeof(SlabHeader);
            cur       = align_up(cur, obj_align);
            auto end  = base + slab_bytes;

            size_t total = 0;
            auto p       = cur;
            while (p + obj_size <= end) {
                total++;
                p += obj_size;
            }
            return total;
        }
        void debug_print() const {
            std::cout << "Partial: " << size_of_partial()
                      << ", Full: " << size_of_full()
                      << ", Empty: " << size_of_empty() << std::endl;
        };

    private:
        util::IntrusiveList<SlabHeader> partial{};
        util::IntrusiveList<SlabHeader> full{};
        util::IntrusiveList<SlabHeader> empty{};

        size_t obj_size;
        size_t obj_align;
        size_t pages;
        size_t slab_bytes;

        SlabHeader *new_slab();
        void init_slab_headers(SlabHeader *slab);

        void update_slab_state(SlabHeader *slab);
        void to_empty(SlabHeader *slab);
        void to_partial(SlabHeader *slab);
        void to_full(SlabHeader *slab);
    };
    
    struct BigAllocatorHeader{
        int magic;
        size_t pages;
        void *raw_base;
    };

    class SlubAllocator {
    public:
        static constexpr int kMax = 2048;
        static constexpr int kNum = 9;  // 8..2048 => 8*2^0 .. 8*2^8
        SlubAllocator();
        void *alloc(size_t n);
        void free(void *ptr);
        void free(void *ptr, size_t size);

    private:
        SlubCache caches[kNum];
        size_t slab_bytes;
        
        static int class_index(size_t n) {
            n        = std::max<size_t>(n, 8);
            size_t x = 8;
            size_t i = 0;
            while (x < n) {
                x <<= 1;
                i++;
            }
            return i;
        }
    };

}  // namespace slub
