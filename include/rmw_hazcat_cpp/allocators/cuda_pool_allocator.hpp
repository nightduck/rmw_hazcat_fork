#include "hma_template.hpp"
#include <cuda_runtime_api.h>
#include <cuda.h>
#include <cstring>

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

#if defined(__linux__)
struct ipcHandle_st {
    int socket;
    char *socketName;
};
typedef int ShareableHandle;
#elif defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)
struct ipcHandle_st {
    std::vector<HANDLE> hMailslot; // 1 Handle in case of child and `num children` Handles for parent.
};
typedef HANDLE ShareableHandle;
#endif

template<class T, size_t POOL_SIZE>
class StaticGPUPoolAllocator : public HMAAllocator<CUDA_Mem>,
  public AllocatorFactory<StaticGPUPoolAllocator<T, POOL_SIZE>>
{
public:
  StaticGPUPoolAllocator(int id)
  {
    shmem_id = id;
    this->dealloc_fn = &StaticGPUPoolAllocator::static_deallocate;
    this->remap_fn = &StaticGPUPoolAllocator::static_remap;
    count = 0;
    rear_it = 0;

    CUmemAllocationProp props = {};
    props.type = CU_MEM_ALLOCATION_TYPE_PINNED;
    props.location.type = CU_MEM_LOCATION_TYPE_DEVICE;
    props.location.id = 0;
    props.requestedHandleTypes = CU_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR;
    size_t gran;
    CHECK_DRV(cuMemGetAllocationGranularity(&gran, &props, CU_MEM_ALLOC_GRANULARITY_MINIMUM));

    int devCount;
    CHECK_DRV(cuDeviceGetCount(&devCount));

    CUdevice device;
    CHECK_DRV(cuDeviceGet(&device, 0));

    constexpr size_t rough_size = sizeof(T) * POOL_SIZE;
    size_t remainder = rough_size % gran;
    pool_size = (remainder == 0) ? rough_size : rough_size + gran - remainder;

    CHECK_DRV(cuMemCreate(&original_handle, pool_size, &props, device));

    // Export to create shared handle.
    CHECK_DRV(cuMemExportToShareableHandle((void *)&ipc_handle, original_handle,
      CU_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR, 0));

    // Unclear what to do with original (might be able to release it if it's mapped?)
  }

  ~StaticGPUPoolAllocator()
  {
    struct shmid_ds buf;
    if (shmctl(shmem_id, IPC_STAT, &buf) == -1) {
      std::cout << "Destruction failed on fetching segment info" << std::endl;
      //RMW_SET_ERROR_MSG("Error reading info about shared StaticGPUPoolAllocator");
      //return RMW_RET_ERROR;
      return;
    }

    if (buf.shm_cpid == getpid()) {
      CHECK_DRV(cuMemRelease(original_handle));
      CHECK_DRV(cuMemUnmap((CUdeviceptr)(this + 1), pool_size));

      std::cout << "Marking segment fo removal" << std::endl;
      if (shmctl(shmem_id, IPC_RMID, NULL) == -1) {
        std::cout << "Destruction failed on marking segment for removal" << std::endl;
        //RMW_SET_ERROR_MSG("can't mark shared StaticGPUPoolAllocator for deletion");
        //return RMW_RET_ERROR;
        return;
      }
    }

    CHECK_DRV(cuMemAddressFree((CUdeviceptr)this, 0x80000000));
    if (shmdt(this) == -1) {
      std::cout << "Destruction failed on detach" << std::endl;
    }
  }

  void * remap_shared_alloc_and_pool() override
  {
    // TODO: This is breaking on "invalid device ordinal". Something about passing a 0 to
    // cuMemCreate above. Look at this: https://github.com/NVIDIA/cuda-samples/blob/b312abaa07ffdc1ba6e3d44a9bc1a8e89149c20b/Samples/3_CUDA_Features/memMapIPCDrv/memMapIpc.cpp#L419
    // and investigate device IDs with cuDeviceGetCount, cuDeviceGet, etc
    // Get shareable handle
    CUmemGenericAllocationHandle handle;
    CHECK_DRV(cuMemImportFromShareableHandle(&handle, &ipc_handle,
      CU_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR));

    // Create CUDA allocation and remap self
    CUdeviceptr d_addr;
    CHECK_DRV(cuMemAddressReserve(&d_addr, 0x80000000, 0, 0, 0ULL));

    // cuMemMap (with offset?)
    CHECK_DRV(cuMemMap(d_addr + sizeof(this), pool_size, 0, handle, 0));

    // cuMemSetAccess
    CUmemAccessDesc accessDesc;
    accessDesc.location = {CU_MEM_LOCATION_TYPE_DEVICE, 0};
    accessDesc.flags = CU_MEM_ACCESS_FLAGS_PROT_READWRITE;
    CHECK_DRV(cuMemSetAccess(d_addr + sizeof(this), pool_size, &accessDesc, 1));

    // Free handle. Memory will stay valid as long as it is mapped
    CHECK_DRV(cuMemRelease(handle));

    // shmat into the beginning of d_addr, with SHM_RDONLY | SHM_REMAP flags
    void * new_this = shmat(shmem_id, (void*)d_addr, SHM_REMAP);
    shmdt(this);  // Spicy self removal
    return new_this;
  }

  // Allocates a chunk of memory. Argument is a syntactic formality, will be ignored
  int allocate(size_t size = 0)
  {
    if (count == POOL_SIZE) {
      // Allocator full
      return -1;
    } else {
      int forward_it = (rear_it + count) % POOL_SIZE;

      // Give address relative to shared object
      int ret = PTR_TO_OFFSET(this, sizeof(this) + sizeof(T) * forward_it);

      // Update count of how many elements in pool
      count++;

      return ret;
    }
  }

protected:
  // Integer should be offset to pool array
  void deallocate(int offset) override
  {
    if (count == 0) {
      return;       // Allocator empty, nothing to deallocate
    }
    int entry = (int)(offset - sizeof(this)) / sizeof(T);

    // Do math with imaginary overflow indices so forward_it >= entry >= rear_it
    int forward_it = rear_it + count;
    if (__glibc_unlikely(entry < rear_it)) {
      entry += POOL_SIZE;
    }

    // Most likely scenario: entry == rear_it as allocations are deallocated in order
    rear_it = entry + 1;
    count = forward_it - rear_it;
  }

  void copy_from(void * here, void * there, int size) override
  {
    cudaMemcpy(there, here, size, cudaMemcpyHostToDevice);
  }
  void copy_to(void * here, void * there, int size) override
  {
    cudaMemcpy(here, there, size, cudaMemcpyDeviceToHost);
  }

  int count;
  int rear_it;
  CUmemGenericAllocationHandle original_handle;
  size_t pool_size;
  ShareableHandle ipc_handle;
};
