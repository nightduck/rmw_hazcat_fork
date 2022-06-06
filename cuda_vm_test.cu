#include <cuda_runtime.h>
#include <cuda.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/shm.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <iostream>

static inline void
checkDrvError(CUresult res, const char *tok, const char *file, unsigned line)
{
    if (res != CUDA_SUCCESS) {
        const char *errStr = NULL;
        (void)cuGetErrorString(res, &errStr);
        std::cerr << file << ':' << line << ' ' << tok
                  << "failed (" << (unsigned)res << "): " << errStr << std::endl;
        abort();
    }
}

#define CHECK_DRV(x) checkDrvError(x, #x, __FILE__, __LINE__);

__global__ void square(int * array) {
    int tid = threadIdx.x;
    array[tid] = array[tid] * array[tid];
}

class Thing {
    public:
    Thing(int i) {
        x = i;
        y = i * i;
        z = i * i * i;
    }

private:
    int x,y,z;
};

int main(int argc, char **argv) {
    void * ptr = malloc(12);

    Thing * t = new(ptr) Thing(4);



    int id = shmget(854, 0x400000, IPC_CREAT | 0600);
    if (id == -1) {
        std::cout << "shmget: " << errno << std::endl;
        return -1;
    }
    void * addr = shmat(id, NULL, 0);
    if (addr == (void*)-1) {
        std::cout << "shmat: " << errno << std::endl;
        return -1;
    }
    if (shmctl(id, IPC_RMID, NULL) == -1)
        std::cout << "shmctl: " << errno << std::endl;
    

    std::cout << "Pagesize: " << getpagesize() << std::endl;

    std::cout << "Catch me at " << getpid() << std::endl;

    std::cout << "Doing cuda things" << std::endl;
    CHECK_DRV(cuInit(0));

    CUmemAllocationProp props = {};
    props.type = CU_MEM_ALLOCATION_TYPE_PINNED;
    props.location.type = CU_MEM_LOCATION_TYPE_DEVICE;
    props.location.id = 0;

    CUdeviceptr d_addr;
    CUdeviceptr d_hint = 0x00004dead000000;
    CHECK_DRV(cuMemAddressReserve(&d_addr, 0x2000000, 0x100, 0, 0ULL));

    CUmemGenericAllocationHandle handle;
    CHECK_DRV(cuMemCreate(&handle, 0x200000, &props, 0));

    CHECK_DRV(cuMemMap(d_addr + 0x400000, 0x200000, 0, handle, 0));

    CUmemAccessDesc accessDesc;
    accessDesc.location = props.location;
    accessDesc.flags = CU_MEM_ACCESS_FLAGS_PROT_READWRITE;
    CHECK_DRV(cuMemSetAccess(d_addr + 0x400000, 0x200000, &accessDesc, 1));

    int buffer[] = {1,2,3,4,5,6,7,8};
    cudaMemcpy((void*)d_addr, buffer, 8 * sizeof(int), cudaMemcpyHostToDevice);

    square<<<1, 8>>>((int*)d_addr);
    cudaDeviceSynchronize();

    cudaMemcpy(buffer, (void*)d_addr, 8 * sizeof(int), cudaMemcpyDeviceToHost);
    for(int i = 0; i < 8; i++) {
        std::cout << buffer[i] << " ";
    }


    std::cout << "Shared memory stuff" << std::endl;
    void * adj_addr = shmat(id, (void*)(d_addr), SHM_RDONLY | SHM_REMAP);
    
    *(int*)addr = 69420;
    if (*(int*)adj_addr == 69420) {
        std::cout << "Mapping successful" << std::endl;
    }

    CHECK_DRV(cuMemUnmap(d_addr + 0x400000, 0x200000));
    CHECK_DRV(cuMemRelease(handle));
    CHECK_DRV(cuMemAddressFree(d_addr, 0x2000000));

    shmdt(addr);
    shmdt(adj_addr);
    close(id);



    return 0;
}