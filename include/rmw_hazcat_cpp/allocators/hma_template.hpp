#include <sys/shm.h>

typedef CPU_Mem;
typedef GPU_Mem;

template<typename MemoryDomain>
class HMAAllocator {
public:
    // Takes same arguments as constructor, allocates shared memory that may be used by constructor
    // with placement new
    template<typename... Args>
    static void * create_alloc(Args... args) {
        int key = 56;
        size_t size = compute_size(args);

        if(!shmget(key, size, IPC_CREAT)) {
            // TODO: More robust error checking
            return NULL;
        }

        // TODO: shmctl to set permissions and such

        // TODO: Link the library for the allocator so code is visible too
        return shmat(key, NULL, 0);
    }
    
    void * allocate(size_t size);

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
    virtual void * copy_from(void * ptr, int size);
    virtual void * copy_to(void * ptr, int size);

    // Takes same arguments as constructor and computes it's final size
    template<typename... Args>
    virtual int compute_size(Args... args);
    
    template<typename T>
    void * copy(void * ptr, int size, HMAAllocator<T> * alloc) {

    }
};