#include "hma_template.hpp"

template<class T, size_t POOL_SIZE> 
class StaticPoolAllocator : public AllocatorFactory<StaticPoolAllocator<T, POOL_SIZE>>,
                            public HMAAllocator<CPU_Mem> {
public:
    StaticPoolAllocator(int id) {
        shmem_id = id;
        dealloc_fn = &StaticPoolAllocator::static_deallocate;
        remap_fn = &StaticPoolAllocator::static_remap;
        forward_it = 0;
        rear_it = -1;
    }

    ~StaticPoolAllocator() {
        struct shmid_ds buf;
        if(shmctl(shmem_id, IPC_STAT, &buf) == -1) {
            //RMW_SET_ERROR_MSG("Error reading info about shared StaticPoolAllocator");
            //return RMW_RET_ERROR;
            return;
        }

        if(buf.shm_cpid == getpid()) {
            if(shmctl(shmem_id, IPC_RMID, NULL) == -1) {
                //RMW_SET_ERROR_MSG("can't mark shared StaticPoolAllocator for deletion");
                //return RMW_RET_ERROR;
                return;
            }
        }
        shmdt(this);
    }

    void * remap_shared_alloc_and_pool() override {
        return this;
    }

    // TODO: Reconsider starting conditions of iterators being -1 and 0
    // Allocates a chunk of memory. Argument is a syntactic formality, will be ignored
    void * allocate(size_t size = sizeof(T)) {
        int next = (forward_it + 1 >= size) ? 0 : forward_it + 1;
        if (next == rear_it) {
            return NULL;
        } else {
            forward_it = next;

            // Give address relative to shared object
            return (void*)((intptr_t)&pool[next] - (intptr_t)this);
        }
    }

protected:
    // TODO: Reconsider starting conditions of iterators being -1 and 0
    void deallocate(void * ptr) override {
        int index = ((intptr_t)ptr - (intptr_t)pool) / sizeof(T);
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

private:
    // These will never get called
    void copy_from(void * here, void * there, int size) override {
        there = here;
    }
    void copy_to(void * here, void * there, int size) override {
        here = there;
    }

    int forward_it;
    int rear_it;
    T pool[POOL_SIZE];
};