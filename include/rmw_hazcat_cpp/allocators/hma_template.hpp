#include <sys/shm.h>
#include <unistd.h>

#define MAX_POOL_SIZE   0x100000000

typedef CPU_Mem;
typedef CUDA_Mem;

template<typename MemoryDomain>
class HMAAllocator {
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

    // Takes same arguments as constructor (except id at beginning), allocates shared memory that
    // may be used by constructor with placement new
    template<typename AllocT, typename... Args>
    static AllocT * create_shared_alloc(Args... args) {

        // Create shared memory block
        int id = shmget(IPC_PRIVATE, sizeof(AllocT), 0640)
        if (id == (void*) -1) {
            // TODO: More robust error checking
            return NULL;
        }

        // Construct allocator in shared memory, and allocate (but don't map) optional memory pool
        void * ptr = shmat(id, NULL, 0);
        AllocT * alloc = new(ptr) AllocT(id, args);

        // TODO: shmctl to set permissions and such

        // Optionally create a pool in host or device memory, and remap self and pool to be
        // adjacent in virtual memory
        return remap_shared_alloc_and_pool(alloc);
    }

    // Map in a shared allocator, then let it reserve itself a memory pool and remap itself
    // to be adjacent to that
    static void * map_shared_alloc(int shm_id) {
        void * addr = shmat(shm_id, NULL, 0);
        return ((HMAAllocator*)addr)->remap_fn(addr);
    }

    template<typename T>
    void * convert(void* ptr, int size, HMAAllocator<T> * alloc) {
        if (std::is_equal<MemoryDomain, T>) {
            // Zero copy condition
            src = dest;
            return;
        } else {
            // If not present in this domain, see if the necessary copy is CPU-to-other,
            // other-to-CPU or other-to-other.
            void * here = this->allocate(size);
            if (std::is_equal<CPU_Mem, T>) {
                this->copy_to(here, ptr, size);
            } else if (std::is_equal<MemoryDomain, CPU_Mem>) {
                alloc->copy_from(ptr, here, size);
            } else {
                copy(here, ptr, size, alloc);
            }
            return here;
        }
    }

protected:
    int shmem_id;
    void (*dealloc_fn)(void*,void*);    // Set to static_deallocate
    void (*remap_fn)(void*);            // Set to remap_shared_alloc_and_pool

    virtual void deallocate(void * ptr);

    // Copy from self to main memory
    virtual void copy_from(void * here, void * there, int size);

    // Copy to self from many memory
    virtual void copy_to(void * here, void * there, int size);
    
    // Default copy operation between to non-CPU domains, using (dynamically alloc'ed) CPU as
    // intermediary. Can override with type-specific implementations to bypass main memory
    template<typename T>
    void copy(void * here, void * there, int size, HMAAllocator<T> * alloc) {
        void * interm = malloc(size);
        this->copy_from(here, interm, size);
        alloc->copy_to(there, interm, size);
    }
};

// These will never get called for a CPU based allocator, but predefine them anyways
class HMAAllocator<CPU_Mem> {
    void copy_from(void * here, void * there, int size) {
        there = here;
    }
    void * copy_to(void * here, void * there, int size) {
        here = there;
    }
};