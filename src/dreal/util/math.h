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

#include <cstdint>

namespace dreal {
/// Returns true if @p v is represented by `int`.
bool is_integer(double v);

/// Converts @p v of int64_t to int.
/// @throw std::runtime_error if this conversion result in a loss of precision.
int convert_int64_to_int(std::int64_t v);

/// Converts @p v of int64_t to double.
/// @throw std::runtime_error if this conversion result in a loss of precision.
double convert_int64_to_double(std::int64_t v);


/**
 * This is a fixed-increment version of Java 8's SplittableRandom generator.
 * Taken from: https://github.com/svaarala/duktape/blob/50af773b1b32067170786c2b7c661705ec7425d4/misc/splitmix64.c#L11-L28
 * http://dx.doi.org/10.1145/2714064.2660195
 * http://docs.oracle.com/javase/8/docs/api/java/util/SplittableRandom.html
 *
 * @param state - Can be seeded with any value. Must be preserved between invocations.
 * @return Next random value in splitmix64 sequence.
 */
uint64_t fast_random_next(uint64_t& state);

}  // namespace dreal
