#pragma once

#include <list.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
namespace slub {

    constexpr size_t PAGE_SIZE = 4096;

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

    struct SlabHeader {
        enum class SlabState { EMPTY, PARTIAL, FULL };
        SlabHeader *prev{};
        SlabHeader *next{};
        void *freelist{};
        uint32_t inuse{};
        uint32_t total{};
        SlabState state{};
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

    class SlubCache {
    public:
        SlubCache(size_t obj_size, size_t obj_align, size_t pages_per_slab = 1)
            : obj_size(std::max(obj_size, sizeof(void *))),
              obj_align(std::max(obj_align, alignof(void *))),
              pages(pages_per_slab),
              slab_bytes(pages_per_slab * PAGE_SIZE) {
            assert((slab_bytes & (slab_bytes - 1)) == 0 &&
                   "slab_bytes must be a power of 2");
        }
        void *alloc();
        void free(void *ptr);

        size_t object_size() const {
            return obj_size;
        }
        SlabHeader *slab_of(void *p);

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

    class SlubAllocator {
    public:
        static constexpr int kMax = 2048;
        static constexpr int kNum = 9;  // 8..2048 => 8*2^0 .. 8*2^8
        SlubAllocator();
        void *alloc(size_t n);
        void free(void *ptr, size_t size);

    private:
        SlubCache caches[kNum];
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
