#include "rmw_hazcat_cpp/allocators/hma_template.hpp"

template<class T> 
class StaticPoolAllocator : public HMAAllocator<CPU_Mem> {
public:
    StaticPoolAllocator(size_t pool_size) {
        forward_it = rear_it = 0;
        size = pool_size;
        pool = this + 3 * sizeof(int);  // Pool takes up remainder of class size
    }

    // Allocates a chunk of memory. Argument is a syntactic formality, will be ignored
    void * allocate(size_t size = sizeof(T)) {
        int next = (forward_it + 1 >= size) ? 0 : forward_it + 1;
        if (next == rear_it) {
            return NULL;
        } else {
            forward_it = next;
            return &pool[next];
        }
    }

private:
    

    int forward_it;
    int rear_it;
    int size;
    T pool[];
};