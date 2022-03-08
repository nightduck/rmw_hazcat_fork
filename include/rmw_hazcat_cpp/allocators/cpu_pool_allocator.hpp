#include "rmw_hazcat_cpp/allocators/hma_template.hpp"

template<class T, size_t POOL_SIZE> 
class StaticPoolAllocator : public HMAAllocator<CPU_Mem> {
public:
    StaticPoolAllocator(int id) {
        shmem_id = id;
        dealloc_fn = &this->static_deallocate<StaticPoolAllocator<T, POOL_SIZE>>;
        remap_fn = &this->remap_shared_alloc_and_pool;

        forward_it = 0;
        rear_it = -1;
    }

    ~StaticPoolAllocator(int id) {
        struct shmid_ds buf;
        if(shmctl(shmem_id, IPC_STAT, &buf) == -1) {
            RMW_SET_ERROR_MSG("Error reading info about shared StaticPoolAllocator")
        }

        if(buf.shm_cpid == getpid()) {
            if(shmctl(shmem_id, IPC_RMID, NULL) == -1) {
                RMW_SET_ERROR_MSG("can't mark shared StaticPoolAllocator for deletion");
                return RMW_RET_ERROR;
            }
        }
        shmdt(this);
    }

    static void * remap_shared_alloc_and_pool(StaticPoolAllocator<T, POOL_SIZE> * alloc) {
        return alloc;
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

    int forward_it;
    int rear_it;
    T pool[POOL_SIZE];
};