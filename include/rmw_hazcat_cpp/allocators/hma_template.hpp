#include <sys/shm.h>

#define MAX_POOL_SIZE   0x100000000

typedef CPU_Mem;
typedef CUDA_Mem;

template<typename MemoryDomain>
class HMAAllocator {
public:
    // Takes same arguments as constructor (except id at beginning), allocates shared memory that
    // may be used by constructor with placement new
    template<typename AllocT, typename... Args>
    static AllocT * create_shared_alloc(Args... args) {

        // Create shared memory block
        int id = shmget(IPC_PRIVATE, sizeof(AllocT), 0)
        if (id == (void*) -1) {
            // TODO: More robust error checking
            return NULL;
        }

        // Construct allocator in shared memory
        void * ptr = shmat(id, NULL, 0);
        AllocT * alloc = new(ptr) AllocT(args);

        // TODO: shmctl to set permissions and such

        // Optionally create a pool in host or device memory, and remap self and pool to be
        // adjacent in virtual memory
        return remap_shared_alloc_and_pool(alloc);
    }

    // Map in a shared allocator, then let it reserve itself a memory pool and remap itself
    // to be adjacent to that
    static void map_shared_alloc(int shm_id) {
        void * addr = shmat(shm_id, NULL, 0);
        ((HMAAllocator*)addr)->remap_fn(addr);
    }

    // Reserve address space for optional memory pool and then map self to be just prior
    // to it. The relative positioning of the allocator and it's memory pool must be guaranteed
    // to be consistant in every process. If remapping occurs, alloc must be detached before
    // returning. No cleanup needed on part of caller
    template<class AllocT>
    virtual static void * remap_shared_alloc_and_pool(AllocT * alloc);
    
    virtual void * allocate(size_t size);

    // Static wrapper for deallocate
    template<class AllocT>
    typename std::enable_if<std::is_base_of<HMAAllocator, AllocT>::value>::type
    static void static_deallocate(AllocT * alloc, void * ptr) {
        return alloc->deallocate(ptr);
    }

    template<typename T>
    void * convert(void* ptr, int size, HMAAllocator<T> * alloc) {
        if (std::is_equal<MemoryDomain, T>) {
            return ptr;
        } else if (std::is_equal<MemoryDomain, CPU_Mem>) {
            return alloc->copy_from(ptr, size);
        } else if (std::is_equal<MemoryDomain, T>) {
            return this->copy_to(ptr, size);
        } else {
            return copy(ptr, size, alloc);
        }
    }

private:
    int shmem_id;
    void (*dealloc_fn)(void*,void*);    // Set to static_deallocate
    void (*remap_fn)(void*);            // Set to remap_shared_alloc_and_pool
    char buff[];

    virtual void deallocate(void * ptr);

    // Copy from self to main memory
    virtual void copy_from(void * here, void * there, int size);

    // Copy to self from many memory
    virtual void copy_to(void * here, void * there, int size);
    
    // Default copy operation between to non-CPU domains, using (dynamically alloc'ed) CPU as
    // intermediary. Can override with type-specific implementations
    template<typename T>
    void copy(void * here, void * there, int size, HMAAllocator<T> * alloc) {
        void * interm = malloc(size);
        this->copy_from(here, interm, size);
        alloc->copy_to(there, interm, size);
    }
};