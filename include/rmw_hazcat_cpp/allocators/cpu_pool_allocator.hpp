#include "rmw_hazcat_cpp/allocators/hma_template.hpp"

template<class T, size_t POOL_SIZE> 
class StaticPoolAllocator : public HMAAllocator<CPU_Mem> {
public:
    StaticPoolAllocator() {
        forward_it = 0;
        rear_it = -1;
        dealloc_fn = &this->static_deallocate;
    }

    // TODO: Reconsider starting conditions of iterators being -1 and 0
    // Allocates a chunk of memory. Argument is a syntactic formality, will be ignored
    void * allocate(size_t size = sizeof(T)) {
        int next = (forward_it + 1 >= size) ? 0 : forward_it + 1;
        if (next == rear_it) {
            return NULL;
        } else {
            forward_it = next;
            return &pool[next] - this;  // Give address relative to shared object
        }
    }

    static void static_deallocate(StaticPoolAllocator * alloc, void * ptr) {
        alloc->deallocate(ptr);
    }

private:
    // TODO: Reconsider starting conditions of iterators being -1 and 0
    void deallocate(void * ptr) {
        int index = (ptr - &pool) / sizeof(T);
        if(forward_it > index && index > rear_it) {
            rear_it = index;
            return;
        } else if (index > rear_it && rear_it > forward_it) {
            rear_it = index;
            return;
        } else if (rear_it > forward_it && forward_it > index) {
            rear_it = index;
            return;
        }
    }


    void * copy_from(void * ptr, int size) {
        return ptr;
    }
    void * copy_from(void * ptr, int size) {
        return ptr;
    }

    int forward_it;
    int rear_it;
    T pool[POOL_SIZE];
};