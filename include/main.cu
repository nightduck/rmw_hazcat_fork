#include <cuda_runtime.h>
#include <cuda.h>
#include "rmw_hazcat_cpp/allocators/cpu_pool_allocator.hpp"
#include <iostream>

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
    AllocT * cpu_alloc;
    void * ptr;

    // DerivedClass c = DerivedClass(4);
    // DerivedClass::static_fn(3);


    cpu_alloc = AllocT::create_shared_alloc();

    cpu_alloc->allocate();
    ptr = malloc(sizeof(long));
    void * copy = cpu_alloc->convert(ptr, sizeof(long), cpu_alloc);
    //static_deallocate<AllocT>(cpu_alloc, copy);
    AllocT::static_deallocate(cpu_alloc, copy);

    UnknownAllocator * uk = (UnknownAllocator*)cpu_alloc;
    uk = UnknownAllocator::map_shared_alloc(cpu_alloc->get_id());

    cpu_alloc->~StaticPoolAllocator();

    //cpu_alloc = HMAAllocator<CPU_Mem>::create_shared_alloc<AllocT>();

    //cpu_alloc = AllocT::create_shared_alloc<AllocT>(64);

    //FakeClass * fake = HMAAllocator<CPU_Mem>::create_shared_alloc<FakeClass>();


    return 0;
}