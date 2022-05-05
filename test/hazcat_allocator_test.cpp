// Copyright (c) 2020 by Robert Bosch GmbH. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "rmw_hazcat_cpp/allocators/cpu_pool_allocator.hpp"
#include "test_msgs/msg/bounded_sequences.hpp"
#include "std_msgs/msg/int32.hpp"

#include <gtest/gtest.h>

#include <string>
#include <tuple>
#include <vector>

uint8_t deref(uint8_t * ptr) {
    return *ptr;
}

template<class T, size_t POOL_SIZE>
class TestWrapper : public StaticPoolAllocator<T, POOL_SIZE>
{
public:
    bool id_is(int truth) {
        return this->shmem_id == truth;
    }

    bool dealloc_is(void* truth) {
        return this->dealloc_fn == truth;
    }

    bool remap_is(void* truth) {
        return this->remap_fn == truth;
    }

    bool rear_it_is(int truth) {
        return this->rear_it == truth;
    }

    bool count_is(int truth) {
        return this->count == truth;
    }

    int pool_offset() {
        return (char*)&(this->pool) - (char*)this;
    }
};

TEST(AllocatorTest, creation_test)
{
    using AllocT = StaticPoolAllocator<test_msgs::msg::BasicTypes, 30>;
    AllocT * cpu_alloc = AllocT::create_shared_alloc();

    // Verify numbers mean what they should
    EXPECT_EQ(sizeof(int), 4UL);
    EXPECT_EQ(sizeof(long), 8UL);
    EXPECT_EQ(sizeof(void(*)(void*)), 8UL);

    // Verify alloc was created correctly
    EXPECT_NE(cpu_alloc, nullptr);
    //TODO: Something with checking the shmem_id. EXPECT_EQ(((int*)cpu_alloc))

    // Verify function pointers are correct
    uint8_t* ptr = (uint8_t*)cpu_alloc;
    int id = cpu_alloc->get_id();
    EXPECT_EQ(*(int*)(ptr+8), id);
    EXPECT_EQ(*(uint64_t*)(ptr+16), (uint64_t)&AllocT::static_deallocate);
    EXPECT_EQ(*(uint64_t*)(ptr+24), (uint64_t)&AllocT::static_remap);

    EXPECT_TRUE(((TestWrapper<test_msgs::msg::BasicTypes, 30>*)cpu_alloc)->id_is(id));

    // Verify shared memory is destroyed correctly
    cpu_alloc->~StaticPoolAllocator();
    EXPECT_EQ(shmat(id, NULL, 0), (void*)-1);
    EXPECT_EQ(errno, EINVAL);
}

TEST(AllocatorTest, allocate_rw_test)
{
    using AllocT = StaticPoolAllocator<std_msgs::msg::Int32, 3>;
    AllocT * cpu_alloc = AllocT::create_shared_alloc();

    cpu_alloc->test_probe();

    // First 3 allocations should go through, but the 4th should fail
    int a1 = cpu_alloc->allocate();
    int a2 = cpu_alloc->allocate();
    int a3 = cpu_alloc->allocate();
    int a4 = cpu_alloc->allocate();

    // Integer should be offset to pool array
    // TODO: Rewrite these with sizeof(type) operations, less hardcoded
    int pool_start = ((TestWrapper<std_msgs::msg::Int32, 3>*)cpu_alloc)->pool_offset();
    EXPECT_EQ(a1, pool_start);
    EXPECT_EQ(a2, pool_start + 4);
    EXPECT_EQ(a3, pool_start + 8);
    EXPECT_EQ(a4, -1);

    std_msgs::msg::Int32 * data1 = (std_msgs::msg::Int32*)(cpu_alloc + a1);
    std_msgs::msg::Int32 * data2 = (std_msgs::msg::Int32*)(cpu_alloc + a2);
    std_msgs::msg::Int32 * data3 = (std_msgs::msg::Int32*)(cpu_alloc + a3);

    data1->data = 10;
    data2->data = 20;
    data3->data = 30;

    // Free first two places
    AllocT::static_deallocate(cpu_alloc, a1);
    AllocT::static_deallocate(cpu_alloc, a2);

    // New allocations should reoccupy these places
    int a5 = cpu_alloc->allocate();
    int a6 = cpu_alloc->allocate();
    EXPECT_EQ(a5, a1);
    EXPECT_EQ(a6, a2);


    std_msgs::msg::Int32 * data5 = (std_msgs::msg::Int32*)(cpu_alloc + a5);
    std_msgs::msg::Int32 * data6 = (std_msgs::msg::Int32*)(cpu_alloc + a6);

    // And data should be readable
    EXPECT_EQ(data5->data, 10);
    EXPECT_EQ(data6->data, 20);
    EXPECT_EQ(data3->data, 30);

    cpu_alloc->~StaticPoolAllocator();

    //EXPECT_ANY_THROW(data5->data = 10);
}