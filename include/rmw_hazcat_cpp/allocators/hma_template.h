#include "cpu_pool_allocator.h"

#ifndef HMA_ALLOCATOR_H
#define HMA_ALLOCATOR_H

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/shm.h>
#include <sys/mman.h>


#define OFFSET_TO_PTR(a, o) (uint8_t *)a + o
#define PTR_TO_OFFSET(a, p) (uint8_t *)p - (uint8_t *)a

#define MAX_POOL_SIZE   0x100000000

// alloc_type is 32bit value.
// First 12 bits encode allocator strategy
// Next 12 bits encode device type
// Last 8 bits encode device number. Only used for multiple GPUs, etc. Must be zero for CPU
//
// The last 20 bits collectively encode the memory "domain"
//
// Example definition:  alloc_type x = (alloc_type)(ALLOC_TLSF << 20 | CUDA << 8 | 3);

struct alloc_type {
    union {
        struct {
            uint16_t strategy : 12;
            uint16_t device_type : 12;
            uint8_t device_number;
        };
        struct {
            uint16_t strategy : 12;
            uint32_t domain : 20;
        };
        struct {
            uint32_t alloc_impl : 24;
            uint8_t device_number;
        };
        uint32_t raw;
    };
};

#define ALLOC_STRAT 0x000   // Not for use
#define ALLOC_RING  0x001
#define ALLOC_TLSF  0x002
#define ALLOC_HEAP  0x003

#define DEVICE      0x000   // Not for use
#define CPU         0x001
#define CUDA        0x002


/*
  // Copy paste at head of new allocators
  union {
    struct {
      // Exist in local memory, pointing to static functions
      const int   (*allocate)   (void * self, size_t size);
      const void  (*deallocate) (void * self, int offset);
      const void  (*copy_from)  (void * here, void * there, size_t size);
      const void  (*copy_to)    (void * here, void * there, size_t size);
      const void  (*copy)       (void * here, void * there, size_t size, hma_allocator * dest_alloc);

      // Exist in shared memory
      const int shmem_id;
      const uint32_t alloc_type;
    };
    hma_allocator untyped;
  };

*/

struct hma_allocator {
    // Exist in local memory, pointing to static functions
    union {
        struct {
            const int   (*allocate)   (void * self, size_t size);
            const void  (*deallocate) (void * self, int offset);
            const void  (*copy_from)  (void * there, void * here, size_t size);
            const void  (*copy_to)    (void * there, void * here, size_t size);
            const void  (*copy)       (void * there, void * here, size_t size, hma_allocator * dest_alloc);
        };
        struct local {
            int   (*allocate)   (void * self, size_t size);
            void  (*deallocate) (void * self, int offset);
            void  (*copy_from)  (void * there, void * here, size_t size);
            void  (*copy_to)    (void * there, void * here, size_t size);
            void  (*copy)       (void * there, void * here, size_t size, hma_allocator * dest_alloc);
        };
    };

    // Exist in shared memory
    union {
        struct {
            const int shmem_id;
            const struct {      // 32bit int indicating type of allocator and memory domain
                union {
                    struct {
                        uint16_t strategy : 12;
                        uint16_t device_type : 12;
                        uint8_t device_number;
                    };
                    struct {
                        uint16_t strategy : 12;
                        uint32_t domain : 20;
                    };
                    struct {
                        uint32_t alloc_impl : 24;
                        uint8_t device_number;
                    };
                };
            };
        };
        struct shared {
            int shmem_id;
            struct {
                uint32_t alloc_impl : 24;
                uint8_t device_number;
            };
        };
    };
};

void * convert(void * ptr, size_t size, hma_allocator * alloc_src, hma_allocator * alloc_dest) {
    if (alloc_src->domain == alloc_dest->domain) {
        // Zero copy condition
        return ptr;
    } else {
        // Allocate space on the destination allocator
        void * here = OFFSET_TO_PTR(alloc_dest, alloc_dest->allocate(alloc_dest, size));
        assert(here > alloc_dest);

        if (alloc_src->domain == CPU) {
            alloc_dest->copy_from(here, ptr, size);
        } else if (alloc_dest->domain == CPU) {
            alloc_src->copy_to(here, ptr, size);
        } else {
            alloc_src->copy(here, ptr, size, alloc_dest);
        }
    }
}

int cant_allocate_here(void * self, size_t size) {
    assert(false);
}

// copy_to, copy_from, and copy shouldn't get called on a CPU allocator, but they've been
// implemented here for completeness anyways
void cpu_copy_tofrom(void * there, void * here, size_t size) {
    memcpy(here, there, size);
}
void cpu_copy(void * there, void * here, size_t size, hma_allocator * dest_alloc) {
    dest_alloc->copy_from(there, here, size);
}

void populate_local_fn_pointers(hma_allocator::local * alloc, uint32_t alloc_impl) {
    switch(alloc_impl) {
        case CPU_RINGBUF_IMPL:
            alloc->allocate = cpu_ringbuf_allocate;
            alloc->deallocate = cpu_ringbuf_deallocate;
            alloc->copy_from = cpu_copy_tofrom;
            alloc->copy_to = cpu_copy_tofrom;
            alloc->copy = cpu_copy;
            break;
        default:
            // TODO: Cleaner error handling
            assert(false);
            break;
    }
}

// TODO: Update documentation
// Don't call this outside this library
hma_allocator * create_shared_allocator(void * hint, size_t alloc_size, uint16_t strategy, uint16_t device_type, uint8_t device_number) {
    uint32_t alloc_impl = strategy << 12 | device_type;

    // Construct local portion of allocator
    hma_allocator::local * fps = (hma_allocator::local*)mmap(hint,
        sizeof(hma_allocator::local), PROT_READ | PROT_WRITE, MAP_FIXED | MAP_ANONYMOUS, 0, 0);
    populate_local_fn_pointers(fps, alloc_impl);

    printf("Mounted local portion of alloc at: %xp\n", fps);
    // Create shared memory block (requested size )
    int id = shmget(IPC_PRIVATE, alloc_size - sizeof(hma_allocator::local), 0640);
    if (id == -1) {
      // TODO: More robust error checking
      return nullptr;
    }

    printf("Allocator id: %d\n", id);

    // Construct shared portion of allocator
    hma_allocator::shared* alloc = (hma_allocator::shared*)shmat(id, hint + sizeof(hma_allocator::local), 0);
    alloc->shmem_id = id;
    alloc->alloc_impl = alloc_impl;
    alloc->device_number = device_number;

    printf("Mounted shared portion of alloc at: %xp\n", alloc);

    // TODO: shmctl to set permissions and such

    // Give back base allocator, straddling local and shared memory mappings
    return (hma_allocator *)fps;
}

// TODO: Update documentation
// Do call this
hma_allocator * remap_shared_allocator(int shmem_id) {
    // Temporarily map in shared allocator to read it's alloc_type
    hma_allocator::shared * shared = (hma_allocator::shared*)shmat(shmem_id, NULL, 0);

    hma_allocator * alloc;
    switch (shared->alloc_impl)
    {
    case CPU_RINGBUF_IMPL:
        alloc = cpu_ringbuf_remap(shared);
        break;
    
    default:
        break;
    }

    // TODO: Switch case calling remap function for each alloc type, which map in pool, remap
    //       allocator, populate function pointers, and return pointer to new location

    // Unmap temp mapping, and return pointer from switch case block
    shmdt(shared);

    return alloc;
}


#endif // HMA_ALLOCATOR_H