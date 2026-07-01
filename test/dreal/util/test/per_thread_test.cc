/*
   Copyright 2017 Toyota Research Institute

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
#include "dreal/util/per_thread.h"

#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace dreal {
namespace {

TEST(PerThreadTest, SizeReflectsConstruction) {
  const PerThread<int> pt{4};
  EXPECT_EQ(pt.size(), 4);
}

// On a single thread, the factory runs exactly once and every call returns the
// same instance.
TEST(PerThreadTest, BuildsOncePerThreadAndReturnsSameInstance) {
  const PerThread<int> pt{1024};  // generous: the calling thread's id fits
  int build_count{0};
  const auto make = [&build_count]() {
    ++build_count;
    return std::make_unique<int>(42);
  };
  int& a{pt.GetOrCreate(make)};
  int& b{pt.GetOrCreate(make)};
  EXPECT_EQ(&a, &b) << "the same thread must get the same instance";
  EXPECT_EQ(build_count, 1) << "the factory must run exactly once per thread";
  EXPECT_EQ(a, 42);
}

// Different worker threads get distinct instances (the whole point of the table).
TEST(PerThreadTest, DistinctInstancesAcrossThreads) {
  const PerThread<int> pt{1024};
  std::mutex m;
  std::vector<int*> ptrs;
  const auto worker = [&pt, &m, &ptrs]() {
    int& v{pt.GetOrCreate([]() { return std::make_unique<int>(7); })};
    const std::lock_guard<std::mutex> g{m};
    ptrs.push_back(&v);
  };
  std::thread t1{worker};
  std::thread t2{worker};
  t1.join();
  t2.join();
  ASSERT_EQ(ptrs.size(), 2u);
  EXPECT_NE(ptrs[0], ptrs[1])
      << "different threads must drive different instances";
}

// An out-of-range thread id fails loud (always-on throw), rather than the
// silent heap out-of-bounds the previous hand-rolled dispatchers risked. A
// zero-slot table makes every thread id (>= 0) out of range.
TEST(PerThreadTest, OutOfRangeThreadIdThrows) {
  const PerThread<int> pt{0};
  EXPECT_THROW(pt.GetOrCreate([]() { return std::make_unique<int>(0); }),
               std::runtime_error);
}

}  // namespace
}  // namespace dreal
