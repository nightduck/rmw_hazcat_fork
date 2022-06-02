#ifndef EXAMPLE_ALLOCATOR_H
#define EXAMPLE_ALLOCATOR_H

#define EXAMPLE_IMPL    DEVICE << 12 | ALLOC_STRAT

#include "hma_template.h"
#include <cstring>


struct example_allocator {
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

  // TODO: Populate this portion with any structures to implement your strategy
};

example_allocator * create_example_allocator(int id, size_t item_size, size_t ring_size) {
  example_allocator * alloc = (example_allocator*)create_shared_allocator(
    NULL, sizeof(example_allocator), DEVICE, ALLOC_STRAT, 0);
    // TODO: Change DEVICE and ALLOC_STRAT above

  // TODO: Construct strategy
}

int example_allocate(void* self, size_t size) {
  example_allocator * s = (example_allocator*)self;
  
  // TODO: Implement allocation method

  return 12345;
}

void example_deallocate(void* self, int offset) {
  example_allocator * s = (example_allocator*)self;
  
  // TODO: Implement deallocation method
}

void example_copy_from(void* there, void* here, size_t size) {
  // TODO: Implement method to copy from main memory into self
}

void example_copy_to(void* there, void* here, size_t size) {
  // TODO: Implement method to copy to main memory from self
}

void example_copy(void* there, void* here, size_t size, hma_allocator * dest_alloc) {
  // TODO: Implement method to copy to non-cpu memory from self
}

hma_allocator * example_remap(hma_allocator::shared * temp) {
  // TODO: Find address you want your allocator to start at. Might be determined by the memory
  //       granularity of the device it is managing. Shouldn't overlap with temp
  void* example_hint = NULL;

  // TODO: Maybe reserve an address space if your device API requires it

  // Create a local mapping, and populate function pointers so they resolve in this process
  hma_allocator::local * fps = (hma_allocator::local*)mmap(example_hint,
      sizeof(hma_allocator::local), PROT_READ | PROT_WRITE, MAP_FIXED | MAP_ANONYMOUS, 0, 0);
  populate_local_fn_pointers(fps, EXAMPLE_IMPL);

  // Map in shared portion of allocator
  shmat(temp->shmem_id, fps + sizeof(hma_allocator::local), 0);

  // TODO: Map in memory pool on device memory

  // fps can now be typecast to example_allocator* and work correctly. Updates to any member
  // besides top 40 bytes will be visible across processes
  return (hma_allocator *)fps;
}


#endif // EXAMPLE_ALLOCATOR_H