#include "hma_template.hpp"
#include <cstring>

template<class T, size_t POOL_SIZE> 
class StaticPoolAllocator : public HMAAllocator<CPU_Mem>,
                            public AllocatorFactory<StaticPoolAllocator<T, POOL_SIZE>> {
public:
    StaticPoolAllocator(int id) {
        shmem_id = id;
        this->dealloc_fn = &StaticPoolAllocator::static_deallocate;
        this->remap_fn = &StaticPoolAllocator::static_remap;
        count = 0;
        rear_it = 0;
    }

    ~StaticPoolAllocator() {
        struct shmid_ds buf;
        if(shmctl(shmem_id, IPC_STAT, &buf) == -1) {
            std::cout << "Destruction failed on fetching segment info" << std::endl;
            //RMW_SET_ERROR_MSG("Error reading info about shared StaticPoolAllocator");
            //return RMW_RET_ERROR;
            return;
        }

        if(buf.shm_cpid == getpid()) {
            std::cout << "Marking segment fo removal" << std::endl;
            if(shmctl(shmem_id, IPC_RMID, NULL) == -1) {
                std::cout << "Destruction failed on marking segment for removal" << std::endl;
                //RMW_SET_ERROR_MSG("can't mark shared StaticPoolAllocator for deletion");
                //return RMW_RET_ERROR;
                return;
            }
        }
        if(shmdt(this) == -1) {
            std::cout << "Destruction failed on detach" << std::endl;
        }
    }

    void * remap_shared_alloc_and_pool() override {
        return this;
    }

    // Allocates a chunk of memory. Argument is a syntactic formality, will be ignored
    int allocate(size_t size = 0) {
        if (count == POOL_SIZE) {
            // Allocator full
            return -1;
        } else {
            int forward_it = (rear_it + count) % POOL_SIZE;

            // Give address relative to shared object
            int ret = PTR_TO_OFFSET(this, &pool[forward_it]);

            // Update count of how many elements in pool
            count++;

            return ret;
        }
    }

protected:
    // Integer should be offset to pool array
    void deallocate(int offset) override {
        if (count == 0) {
            return; // Allocator empty, nothing to deallocate
        }
        int entry = (int)((uint8_t*)this + offset - (uint8_t*)pool) / sizeof(T);

        // Do math with imaginary overflow indices so forward_it >= entry >= rear_it
        int forward_it = rear_it + count;
        if (__glibc_unlikely(entry < rear_it)) {
            entry += POOL_SIZE;
        }

        // Most likely scenario: entry == rear_it as allocations are deallocated in order
        rear_it = entry + 1;
        count = forward_it - rear_it;

        return;
    }

    // These will never get called
    void copy_from(void * here, void * there, int size) override {
        std::memcpy(there, here, size);
    }
    void copy_to(void * here, void * there, int size) override {
        std::memcpy(here, there, size);
    }

    int count;
    int rear_it;
    T pool[POOL_SIZE];
};