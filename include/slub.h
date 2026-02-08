#pragma once

#include <list.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace slub {

    constexpr size_t PAGE_SIZE      = 4096;
    constexpr size_t PAGES_PER_SLAB = 1;
    constexpr size_t SLAB_BYTES     = PAGE_SIZE * PAGES_PER_SLAB;
    constexpr size_t ALIGN          = 16;

    struct Buddy {
        static void *alloc_pages(size_t pages);
        static void free_pages(void *p);
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
        size_t inuse{};
        size_t total{};
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

    template <typename ObjType>
    struct size_of_type
        : public std::integral_constant<size_t, sizeof(ObjType)> {};

    template <typename ObjType>
    struct align_of_type
        : public std::integral_constant<size_t, alignof(ObjType)> {};

    template <typename ObjType>
    class SlubAllocator {
    protected:
        static constexpr size_t raw_obj_size_  = size_of_type<ObjType>::value;
        static constexpr size_t raw_obj_align_ = align_of_type<ObjType>::value;

        static constexpr size_t ptr_size_  = sizeof(void *);
        static constexpr size_t ptr_align_ = alignof(void *);

        static constexpr size_t max_const(size_t a, size_t b) {
            return a > b ? a : b;
        }

        // Round up n to multiple of align; align must be power-of-two.
        static constexpr size_t round_up_pow2(size_t n, size_t align) {
            return (n + align - 1) & ~(align - 1);
        }

        // Free-list next pointer is stored in object body, so size/alignment
        // must be at least pointer-sized/pointer-aligned.
        static constexpr size_t obj_align_ =
            max_const(raw_obj_align_, ptr_align_);
        static constexpr size_t obj_size_ =
            round_up_pow2(max_const(raw_obj_size_, ptr_size_), obj_align_);

        constexpr static size_t pages_      = PAGES_PER_SLAB;
        constexpr static size_t slab_bytes_ = SLAB_BYTES;

        static_assert((obj_align_ & (obj_align_ - 1)) == 0,
                      "obj_align_ must be power-of-two");

    public:
        static constexpr int kMax = 2048;
        SlubAllocator();
        void *alloc();
        void free(void *ptr);

    private:
        util::IntrusiveList<SlabHeader> partial{};
        util::IntrusiveList<SlabHeader> full{};
        util::IntrusiveList<SlabHeader> empty{};

        SlabHeader *new_slab();
        void init_slab_headers(SlabHeader *slab);
        SlabHeader *slab_of(void *p);

        void to_empty(SlabHeader *slab);
        void to_partial(SlabHeader *slab);
        void to_full(SlabHeader *slab);

        void inner_free(void *ptr);
    };

    template <typename ObjType>
    SlabHeader *SlubAllocator<ObjType>::slab_of(void *p) {
        auto ptr  = reinterpret_cast<uintptr_t>(p);
        auto base = align_down(ptr, slab_bytes_);
        return reinterpret_cast<SlabHeader *>(base);
    }

    template <typename ObjType>
    void SlubAllocator<ObjType>::init_slab_headers(SlabHeader *slab) {
        auto base = reinterpret_cast<uintptr_t>(slab);
        auto cur  = base + sizeof(SlabHeader);
        cur       = align_up(cur, obj_align_);
        auto end  = base + slab_bytes_;

        size_t total = 0;
        auto p       = cur;
        while (p + obj_size_ <= end) {
            total++;
            p += obj_size_;
        }

        slab->total = total;
        slab->inuse = 0;

        void *head = nullptr;

        // construct from end to start
        for (size_t i = total; i > 0; i--) {
            void *obj = reinterpret_cast<void *>(cur + i * obj_size_);
            *reinterpret_cast<void **>(obj) = head;
            head                            = obj;
        }
        slab->freelist = head;
    }

    template <typename ObjType>
    SlabHeader *SlubAllocator<ObjType>::new_slab() {
        void *mem        = Buddy::alloc_pages(pages_);
        SlabHeader *slab = new (mem) SlabHeader{};
        init_slab_headers(slab);
        return slab;
    }

    template <typename ObjType>
    void SlubAllocator<ObjType>::to_empty(SlabHeader *slab) {
        if (slab->state == SlabHeader::SlabState::PARTIAL) {
            partial.erase(typename decltype(partial)::iterator(slab));
        } else if (slab->state == SlabHeader::SlabState::FULL) {
            full.erase(typename decltype(full)::iterator(slab));
        }
        slab->state = SlabHeader::SlabState::EMPTY;
        empty.push_back(*slab);
    }

    template <typename ObjType>
    void SlubAllocator<ObjType>::to_partial(SlabHeader *slab) {
        if (slab->state == SlabHeader::SlabState::EMPTY) {
            empty.erase(typename decltype(empty)::iterator(slab));
        } else if (slab->state == SlabHeader::SlabState::FULL) {
            full.erase(typename decltype(full)::iterator(slab));
        }
        slab->state = SlabHeader::SlabState::PARTIAL;
        partial.push_back(*slab);
    }

    // to_full remains mostly same but for consistency safety
    template <typename ObjType>
    void SlubAllocator<ObjType>::to_full(SlabHeader *slab) {
        if (slab->state == SlabHeader::SlabState::PARTIAL) {
            partial.erase(typename decltype(partial)::iterator(slab));
        } else if (slab->state == SlabHeader::SlabState::EMPTY) {
            // Should not happen for to_full usually
            empty.erase(typename decltype(empty)::iterator(slab));
        }
        slab->state = SlabHeader::SlabState::FULL;
        full.push_back(*slab);
    }

    template <typename ObjType>
    void *SlubAllocator<ObjType>::alloc() {
        if (obj_size_ > kMax) {
            size_t pages = (obj_size_ + PAGE_SIZE - 1) / PAGE_SIZE;
            return Buddy::alloc_pages(pages);
        }
        SlabHeader *slab = nullptr;
        if (!partial.empty()) {
            slab = &partial.back();
            assert(slab != nullptr);
        } else if (!empty.empty()) {
            slab = &empty.back();
            assert(slab != nullptr);
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

    template <typename ObjType>
    void SlubAllocator<ObjType>::inner_free(void *ptr) {
        if (!ptr) {
            printf("can't free null pointer\n");
            return;
        }
        SlabHeader *slab_header         = slab_of(ptr);
        *reinterpret_cast<void **>(ptr) = slab_header->freelist;
        slab_header->freelist           = ptr;
        slab_header->inuse--;
        if (slab_header->inuse == 0) {
            to_empty(slab_header);
        } else if (slab_header->inuse == slab_header->total - 1) {
            to_partial(slab_header);
        }
    }

    template <typename ObjType>
    void SlubAllocator<ObjType>::free(void *ptr) {
        if (!ptr) {
            printf("can't free nullptr\n");
            return;
        }
        if (obj_size_ > kMax) {
            Buddy::free_pages(ptr);
            return;
        }

        inner_free(ptr);
    }

    template <typename ObjType>
    SlubAllocator<ObjType>::SlubAllocator() {}
}  // namespace slub
