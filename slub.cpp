#include "slub.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

    void Buddy::free_pages(void *ptr, size_t) {
        // const size_t bytes = pages * PAGE_SIZE;
        std::free(ptr);
    }

    SlabHeader *SlubCache::slab_of(void *p) {
        auto ptr  = reinterpret_cast<uintptr_t>(p);
        auto base = align_down(ptr, slab_bytes);
        return reinterpret_cast<SlabHeader *>(base);
    }

    void SlubCache::init_slab_headers(SlabHeader *slab) {
        auto base = reinterpret_cast<uintptr_t>(slab);
        auto cur  = base + sizeof(SlabHeader);
        cur       = align_up(cur, obj_align);
        auto end  = base + slab_bytes;

        size_t total = 0;
        auto p       = cur;
        while (p < end) {
            total++;
            p += obj_size;
        }

        slab->total = total;
        slab->inuse = 0;

        void *head = nullptr;
        for (size_t i = 0; i < total; i++) {
            void *obj = reinterpret_cast<void *>(cur + i * obj_size);
            // void *prev
            *reinterpret_cast<void **>(obj) = head;
            head                            = obj;
        }
        slab->freelist = head;
    }

    SlabHeader *SlubCache::new_slab() {
        void *mem        = Buddy::alloc_pages(pages);
        SlabHeader *slab = new (mem) SlabHeader{};
        init_slab_headers(slab);
        return slab;
    }

    void SlubCache::to_empty(SlabHeader *slab) {
        partial.erase(decltype(partial)::iterator(slab));
        slab->state = SlabHeader::SlabState::EMPTY;
        empty.push_back(*slab);
    }

    void SlubCache::to_partial(SlabHeader *slab) {
        if (slab->state == SlabHeader::SlabState::EMPTY) {
            empty.erase(decltype(empty)::iterator(slab));
        }
        if (slab->state == SlabHeader::SlabState::FULL) {
            full.erase(decltype(full)::iterator(slab));
        }
        slab->state = SlabHeader::SlabState::PARTIAL;
        partial.push_back(*slab);
    }

    void SlubCache::to_full(SlabHeader *slab) {
        partial.erase(decltype(partial)::iterator(slab));
        slab->state = SlabHeader::SlabState::FULL;
        full.push_back(*slab);
    }

    void *SlubCache::alloc() {
        SlabHeader slab;
        if (!partial.empty()) {
            slab = partial.back();
        } else if (!empty.empty()) {
            slab = empty.back();
        } else {
            slab = *new_slab();
            to_partial(&slab);
        }

        assert(slab.freelist != nullptr);
        void *obj     = slab.freelist;
        slab.freelist = *reinterpret_cast<void **>(obj);
        slab.inuse++;

        if (slab.inuse == slab.total) {
            to_full(&slab);
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
            return Buddy::alloc_pages((n + PAGE_SIZE - 1) / PAGE_SIZE);
        }

        return caches[class_index(n)].alloc();
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

}  // namespace slub
int main() {}
