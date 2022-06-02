#include <type_traits>
#include <sys/shm.h>
#include <cstdlib>
#include <cstdint>
#include <unistd.h>
#include <iostream>
#include <new>

#define OFFSET_TO_PTR(a, o) (uint8_t *)a + o
#define PTR_TO_OFFSET(a, p) (uint8_t *)p - (uint8_t *)a

#define MAX_POOL_SIZE   0x100000000

#ifndef HMA_ALLOCATOR_HPP
#define HMA_ALLOCATOR_HPP

enum class CPU_Mem;
enum class CUDA_Mem;

template<typename MemoryDomain>
class HMAAllocator
{
public:
  /* Requirements for constructor. This will only be called once
   * 1) Signature: (int id, Args... args)
   * 2) dealloc_fn = &this->static_deallocate<AllocType>
   * 3) remap_fn = &this->remap_shared_alloc_and_pool
   * 4) Optionally allocate pool in physical memory, but don't map it in virtual memory yet
   */

  /* Requirements for destructor. This will be called by every process that maps in the allocator
   * 1) Check if the calling process created this shared object
   * 2) If so, use IPC_RMID command to shmctl to mark for deletion
   * 3) Detach memory pool, if present
   * 4) Release memory pool either if called by creating process (if hw allows continued use of
   *    existing mappings), or by last process to unmap this allocator. Latter needs ref counter
   * 5) shmdt(this)   to detach self from process
   */

  // Reserve address space for optional memory pool and then map self to be just prior
  // to it. The relative positioning of the allocator and it's memory pool must be guaranteed
  // to be consistant in every process that calls this. If remapping occurs, alloc must be
  // detached before returning. No cleanup needed on part of caller
  virtual void * remap_shared_alloc_and_pool() = 0;


  // Returns offset, which is measured relative to allocator. Compute this + offset to get pointer
  virtual int allocate(size_t size) = 0;

  // Static wrapper for deallocate.
  static void static_deallocate(HMAAllocator * alloc, int offset)
  {
    return alloc->deallocate(offset);
  }

  template<typename T>
  void * convert(void * ptr, int size, HMAAllocator<T> * alloc)
  {
    if (std::is_same<MemoryDomain, T>::value) {
      // Zero copy condition
      return ptr;
    } else {
      // If not present in this domain, see if the necessary copy is CPU-to-other,
      // other-to-CPU or other-to-other.
      void * here = OFFSET_TO_PTR(this, this->allocate(size));
      if (std::is_same<CPU_Mem, T>::value) {
        this->copy_to(here, ptr, size);
      } else if (std::is_same<MemoryDomain, CPU_Mem>::value) {
        alloc->copy_from(ptr, here, size);
      } else {
        copy(here, ptr, size, alloc);
      }
      return here;
    }
  }

  int get_id()
  {
    return shmem_id;
  }

protected:
  int shmem_id;
  void (* dealloc_fn)(HMAAllocator *, int);   // Set to static_deallocate

  // Offset is measured relative to allocator. Compute this + offset to get pointer to message
  virtual void deallocate(int offset) = 0;

  // Copy from CPU to self
  virtual void copy_from(void * here, void * there, int size) = 0;

  // Copy to CPU from self
  virtual void copy_to(void * here, void * there, int size) = 0;

  // Default copy operation between to non-CPU domains, using (dynamically alloc'ed) CPU as
  // intermediary. Can override with type-specific implementations to bypass main memory
  template<typename T>
  void copy(void * here, void * there, int size, HMAAllocator<T> * alloc)
  {
    void * interm = malloc(size);
    this->copy_to(here, interm, size);
    alloc->copy_from(there, interm, size);
  }
};


// Used for static functions that can't go in HMAAllocator because typing reasons
template<class AllocT>
class AllocatorFactory
{
public:
  // Takes same arguments as constructor (except id at beginning), allocates shared memory that
  // may be used by constructor with placement new
  template<typename ... Args>
  static AllocT * create_shared_alloc(Args... args)
  {
    //static_assert(std::is_base_of<HMAAllocator, AllocT>::value, "AllocT not derived from HMAAllocator");

    // Create shared memory block
    int id = shmget(IPC_PRIVATE, sizeof(AllocT), 0640);
    if (id == -1) {
      // TODO: More robust error checking
      return nullptr;
    }

    std::cout << "Allocator id: " << id << std::endl;

    // Construct allocator in shared memory, and allocate (but don't map) optional memory pool
    void * ptr = shmat(id, NULL, 0);
    AllocT * alloc = new (ptr) AllocT(id, args ...);

    std::cout << "Mounted alloc at: " << alloc << std::endl;

    // TODO: shmctl to set permissions and such

    // Optionally create a pool in host or device memory, and remap self and pool to be
    // adjacent in virtual memory
    return (AllocT *)alloc->remap_shared_alloc_and_pool();
  }

  static void * static_remap(void * alloc)
  {
    return ((AllocT *)alloc)->remap_shared_alloc_and_pool();
  }

protected:
  void * (*remap_fn)(void *);                   // Set to static_remap in AllocatorFactory
};

class UnknownAllocator : protected HMAAllocator<void>,
  protected AllocatorFactory<UnknownAllocator>
{
public:
  void dealloc(int offset)
  {
    dealloc_fn(this, offset);
  }

  // Map in a shared allocator, then let it reserve itself a memory pool and remap itself
  // to be adjacent to that.
  // Weirdly, this is a static function, calling a non-static member, which is just a static
  // wrapper for a non-static function.
  static UnknownAllocator * map_shared_alloc(int shm_id)
  {
    UnknownAllocator * addr = (UnknownAllocator *)shmat(shm_id, NULL, 0);
    return (UnknownAllocator *)addr->remap_fn(addr);
  }
};


// using HMAAllocator<CPU_Mem>::create_shared_alloc<;

// // These will never get called for a CPU based allocator, but predefine them anyways
// class HMAAllocator<CPU_Mem> {
// public:
//     virtual void * allocate(size_t size);

// private:
//     virtual void deallocate(void * ptr);

//     // These will never get called
//     void copy_from(void * here, void * there, int size) {
//         there = here;
//     }
//     void copy_to(void * here, void * there, int size) {
//         here = there;
//     }
// };

#endif // HMA_ALLOCATOR_HPP