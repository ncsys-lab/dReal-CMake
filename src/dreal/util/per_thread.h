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
#pragma once

#include <memory>
#include <vector>

#include "ThreadPool/ThreadPool.h"

#include "dreal/util/exception.h"

namespace dreal {

/// A fixed-size pool of lazily-constructed, per-thread `T` instances.
///
/// Several contractors / evaluators in dReal wrap a non-thread-safe `T` (an
/// ibex `CtcFwdBwd`/`CtcForAll`, a nested counterexample `Context`, …) so that
/// each parallel-ICP worker drives its OWN instance. They all hand-rolled the
/// same lazy table — `vector<int> ready` + `vector<unique_ptr<T>>` indexed by
/// `ThreadPool::get_thread_id()` — and three of them had no bounds check while
/// two had an off-by-one `<=` (a latent out-of-bounds). `PerThread<T>` is that
/// table, once, with the correct bound.
///
/// Thread-safety: each worker thread holds a distinct id in `[0, num_slots)`
/// (the main thread is 0; pool workers 1..N-1), and the slot vector is sized
/// once at construction and never resized — so concurrent first-touch from
/// different threads writes different slots and is race-free without locking.
/// A thread whose id is outside `[0, num_slots)` (which only happens under
/// nested parallelism or if an instance built for N jobs is driven by a thread
/// from a larger pool) is a real invariant violation and FAILS LOUD, rather than
/// corrupting memory the way the previous unchecked indexing would.
template <typename T>
class PerThread {
 public:
  /// Constructs a table with @p num_slots slots (typically `number_of_jobs()`).
  explicit PerThread(int num_slots) : slots_(num_slots) {}

  PerThread(const PerThread&) = delete;
  PerThread(PerThread&&) = delete;
  PerThread& operator=(const PerThread&) = delete;
  PerThread& operator=(PerThread&&) = delete;
  ~PerThread() = default;

  /// Returns this thread's `T`, building it via @p make (which returns a
  /// `std::unique_ptr<T>`) on first touch. Subsequent calls on the same thread
  /// return the same instance.
  template <typename Factory>
  T& GetOrCreate(const Factory& make) const {
    thread_local const int id{ThreadPool::get_thread_id()};
    if (id < 0 || id >= static_cast<int>(slots_.size())) {
      throw DREAL_RUNTIME_ERROR(
          "PerThread: thread id {} is outside [0, {}). A per-thread instance "
          "built for {} jobs was driven by a thread from a larger pool (nested "
          "parallelism, or reuse across differing job counts).",
          id, slots_.size(), slots_.size());
    }
    std::unique_ptr<T>& slot{slots_[id]};
    if (!slot) {
      slot = make();
    }
    return *slot;
  }

  /// Number of slots (the job count this table was built for).
  int size() const { return static_cast<int>(slots_.size()); }

 private:
  mutable std::vector<std::unique_ptr<T>> slots_;
};

}  // namespace dreal
