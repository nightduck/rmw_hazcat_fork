#include <cuda_runtime.h>
#include <cuda.h>
#include "rmw_hazcat_cpp/allocators/cpu_pool_allocator.hpp"
#include <iostream>
#include <cassert>

template<typename T>
class BaseClass {
public:
    int thing;
    double otherThing;

    template<class AllocT>
    static AllocT * static_fn(int i);

    virtual void virtual_fn(int i );
};

// static void static_fn(int i) {
//     std::cout << "int static_fn: " << i << std::endl;
// }

class DerivedClass : BaseClass<int> {
public:
    DerivedClass(int i) {
        thing = i;
        otherThing = i;
    }

    static void static_fn(int i) {
        std::cout << "int static_fn: " << i << std::endl;
    }

    void virtual_fn(int i ) override {
        thing = i;
    }
};

class SelfDestruct {
public:
    SelfDestruct() {
        std::cout << "Constructing at " << this << std::endl;
        i = 4;
        j = 6;
        free(this);
        return;
    }

    int i, j;
};

using AllocT = StaticPoolAllocator<long, 30>;

int main(int argc, char ** argv) {
    AllocT * cpu_alloc = AllocT::create_shared_alloc();

    assert(sizeof(int) == 4UL);
    assert(sizeof(long) == 8UL);
    assert(sizeof(void(*)(void*)) == 8UL);

    assert(cpu_alloc != nullptr);
    //TODO: Something with checking the shmem_id. EXPECT_EQ(((int*)cpu_alloc))

    uint8_t* ptr = (uint8_t*)cpu_alloc;
    int id = cpu_alloc->get_id();
    // assert(*(uint64_t*)(ptr+4) == (uint64_t)&AllocT::static_deallocate);
    // assert(*(uint64_t*)(ptr+12) == (uint64_t)&AllocT::static_remap);

    std::cout << "Pre destruction, ID is : " << cpu_alloc->get_id() << std::endl;
    cpu_alloc->~StaticPoolAllocator();
    std::cout << "Destructed" << std::endl;
    std::cout << "Post destruction, ID is : " << cpu_alloc->get_id() << std::endl;


    return 0;
}