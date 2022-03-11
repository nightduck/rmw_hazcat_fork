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
        forward_it = 0;
        rear_it = -1;
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
        if (forward_it == rear_it) {
            // Allocator full
            return NULL;
        } else {
            // Give address relative to shared object
            int ret = PTR_TO_OFFSET(this, forward_it);

            // Update forward iterator to point to next empty spo
            forward_it++;
            if(forward_it > POOL_SIZE) {
                forward_it = 0;
            }

            return ret;
        }
    }

protected:
    // TODO: Reconsider starting conditions of iterators being -1 and 0
    void deallocate(int offset) override {
        if (rear_it < 0) {
            return; // Allocator empty, nothing to deallocate
        }
        int entry = (int)((uint8_t*)this + offset - (uint8_t*)pool) / sizeof(T);

        if(entry == forward_it) {
            // Dealloc'ed last entry. Allocator now empty
            forward_it = 0;
            rear_it = -1;
        } else if (forward_it <= rear_it && rear_it <= entry) {
            rear_it = entry;
            return;
        } else if (rear_it <= entry && entry <= forward_it) {
            rear_it = entry;
            return;
        } else if (entry <= forward_it && forward_it <= rear_it) {
            rear_it = entry;
            return;
        } else {
            // Other conditions: f<e<r, e<r<f, and r<f<e all try to deallocate unallocated memory
            return;
        }
    }

private:
    // These will never get called
    void copy_from(void * here, void * there, int size) override {
        std::memcpy(there, here, size);
    }
    void copy_to(void * here, void * there, int size) override {
        std::memcpy(here, there, size);
    }

    int forward_it;
    int rear_it;
    T pool[POOL_SIZE];
};