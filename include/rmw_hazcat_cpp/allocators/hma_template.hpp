#include <sys/shm.h>

typedef CPU_Mem;
typedef CUDA_Mem;

template<typename MemoryDomain>
class HMAAllocator {
public:
    // Takes same arguments as constructor, allocates shared memory that may be used by constructor
    // with placement new
    template<typename AllocT, typename... Args>
    static AllocT * create_shared_alloc(Args... args) {
        int key = 56;
        size_t size = sizeof(AllocT);

        if(!shmget(key, size, IPC_CREAT)) {
            // TODO: More robust error checking
            return NULL;
        }

        // TODO: shmctl to set permissions and such

        // TODO: Link the library for the allocator so code is visible too
        void * ptr = shmat(key, NULL, 0);
        return new(ptr) AllocT(args);
    }

    // Reserve address space for self and optional memory pool
    // Map both shared memory object containg self and memory pool into reservation
    static void * map_shared_alloc(int shm_id);
    
    virtual void * allocate(size_t size);

    // Static wrapper for deallocate
    static virtual void static_deallocate(void * alloc, void * ptr); // {alloc->deallocate(ptr);}

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

    void * resolve(long token);

private:
    int shmem_id;
    void (*dealloc_fn)(void*,void*);    // Set to static_deallocate

    virtual void deallocate(void * ptr);

    virtual void * copy_from(void * ptr, int size);
    virtual void * copy_to(void * ptr, int size);
    
    // Default copy operation between to non-CPU domains, using CPU as intermediary.
    // Can override with type-specific implementations
    template<typename T>
    void * copy(void * ptr, int size, HMAAllocator<T> * alloc) {
        return alloc->copy_to(this->copy_from(ptr, size), size);
    }
};