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

#include <gtest/gtest.h>

#include <string>
#include <tuple>
#include <vector>

uint8_t deref(uint8_t * ptr) {
    return *ptr;
}

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

    // Verify shared memory is destroyed correctly
    cpu_alloc->~StaticPoolAllocator();
    EXPECT_EQ(shmat(id, NULL, 0), (void*)-1);
    EXPECT_EQ(errno, EIDRM);
}