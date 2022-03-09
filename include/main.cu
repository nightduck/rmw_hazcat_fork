#include <cuda_runtime.h>
#include <cuda.h>
#include "rmw_hazcat_cpp/allocators/cpu_pool_allocator.hpp"


class FakeClass {
public:
    int thing;
    double otherThing;

    FakeClass() {
        thing = 5;
        otherThing = 5.6;
    }
};

using AllocT = StaticPoolAllocator<FakeClass, 30>;

int main(int argc, char ** argv) {
    AllocT * cpu_alloc;
    void * ptr;

    cpu_alloc = HMAAllocator<CPU_Mem>::create_shared_alloc<AllocT>();

    //cpu_alloc = AllocT::create_shared_alloc<AllocT>(64);

    //FakeClass * fake = HMAAllocator<CPU_Mem>::create_shared_alloc<FakeClass>();


    return 0;
}