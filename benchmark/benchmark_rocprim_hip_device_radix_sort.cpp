// MIT License
//
// Copyright (c) 2017 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <iostream>
#include <chrono>
#include <vector>
#include <limits>
#include <string>
#include <cstdio>
#include <cstdlib>

// Google Benchmark
#include "benchmark/benchmark.h"
// CmdParser
#include "cmdparser.hpp"
#include "benchmark_utils.hpp"

// HIP API
#include <hip/hip_runtime.h>
#include <hip/hip_hcc.h>

// rocPRIM
#include <rocprim.hpp>

#define HIP_CHECK(condition)         \
  {                                  \
    hipError_t error = condition;    \
    if(error != hipSuccess){         \
        std::cout << "HIP error: " << error << " line: " << __LINE__ << std::endl; \
        exit(error); \
    } \
  }

#ifndef DEFAULT_N
const size_t DEFAULT_N = 1024 * 1024 * 32;
#endif

enum class benchmark_kinds
{
    sort_keys,
    sort_keys_desc,
    sort_pairs,
    sort_pairs_desc
};

namespace rp = rocprim;

template<benchmark_kinds benchmark_kind, class Key, class Value>
auto run_device_radix_sort(void * d_temporary_storage, size_t& temporary_storage_bytes,
                           Key * d_keys_input, Key * d_keys_output,
                           Value *, Value *,
                           size_t size,
                           hipStream_t stream)
    -> typename std::enable_if<benchmark_kind == benchmark_kinds::sort_keys, hipError_t>::type
{
    return rp::device_radix_sort_keys(
        d_temporary_storage, temporary_storage_bytes,
        d_keys_input, d_keys_output, size,
        0, sizeof(Key) * 8,
        stream
    );
}

template<benchmark_kinds benchmark_kind, class Key, class Value>
auto run_device_radix_sort(void * d_temporary_storage, size_t& temporary_storage_bytes,
                           Key * d_keys_input, Key * d_keys_output,
                           Value *, Value *,
                           size_t size,
                           hipStream_t stream)
    -> typename std::enable_if<benchmark_kind == benchmark_kinds::sort_keys_desc, hipError_t>::type
{
    return rp::device_radix_sort_keys_desc(
        d_temporary_storage, temporary_storage_bytes,
        d_keys_input, d_keys_output, size,
        0, sizeof(Key) * 8,
        stream
    );
}

template<benchmark_kinds benchmark_kind, class Key, class Value>
auto run_device_radix_sort(void * d_temporary_storage, size_t& temporary_storage_bytes,
                           Key * d_keys_input, Key * d_keys_output,
                           Value * d_values_input, Value * d_values_output,
                           size_t size,
                           hipStream_t stream)
    -> typename std::enable_if<benchmark_kind == benchmark_kinds::sort_pairs, hipError_t>::type
{
    return rp::device_radix_sort_pairs(
        d_temporary_storage, temporary_storage_bytes,
        d_keys_input, d_keys_output, d_values_input, d_values_output, size,
        0, sizeof(Key) * 8,
        stream
    );
}

template<benchmark_kinds benchmark_kind, class Key, class Value>
auto run_device_radix_sort(void * d_temporary_storage, size_t& temporary_storage_bytes,
                           Key * d_keys_input, Key * d_keys_output,
                           Value * d_values_input, Value * d_values_output,
                           size_t size,
                           hipStream_t stream)
    -> typename std::enable_if<benchmark_kind == benchmark_kinds::sort_pairs_desc, hipError_t>::type
{
    return rp::device_radix_sort_pairs_desc(
        d_temporary_storage, temporary_storage_bytes,
        d_keys_input, d_keys_output, d_values_input, d_values_output, size,
        0, sizeof(Key) * 8,
        stream
    );
}

template<benchmark_kinds benchmark_kind, class T>
void run_benchmark(benchmark::State& state, hipStream_t stream, size_t size)
{
    using key_type = T;
    using value_type = T;

    // Generate data
    std::vector<key_type> keys_input;
    if(std::is_floating_point<key_type>::value)
    {
        keys_input = get_random_data<key_type>(size, (key_type)-1000, (key_type)+1000);
    }
    else
    {
        keys_input = get_random_data<key_type>(
            size,
            std::numeric_limits<key_type>::min(),
            std::numeric_limits<key_type>::max()
        );
    }

    std::vector<value_type> values_input(size);
    std::iota(values_input.begin(), values_input.end(), 0);

    key_type * d_keys_input;
    key_type * d_keys_output;
    HIP_CHECK(hipMalloc(&d_keys_input, size * sizeof(key_type)));
    HIP_CHECK(hipMalloc(&d_keys_output, size * sizeof(key_type)));
    HIP_CHECK(
        hipMemcpy(
            d_keys_input, keys_input.data(),
            size * sizeof(key_type),
            hipMemcpyHostToDevice
        )
    );

    value_type * d_values_input;
    value_type * d_values_output;
    HIP_CHECK(hipMalloc(&d_values_input, size * sizeof(value_type)));
    HIP_CHECK(hipMalloc(&d_values_output, size * sizeof(value_type)));
    HIP_CHECK(
        hipMemcpy(
            d_values_input, values_input.data(),
            size * sizeof(value_type),
            hipMemcpyHostToDevice
        )
    );

    void * d_temporary_storage = nullptr;
    size_t temporary_storage_bytes = 0;
    HIP_CHECK(
        run_device_radix_sort<benchmark_kind>(
            d_temporary_storage, temporary_storage_bytes,
            d_keys_input, d_keys_output, d_values_input, d_values_output, size,
            stream
        )
    );

    HIP_CHECK(hipMalloc(&d_temporary_storage, temporary_storage_bytes));
    HIP_CHECK(hipDeviceSynchronize());

    // Warm-up
    for(size_t i = 0; i < 10; i++)
    {
        HIP_CHECK(
            run_device_radix_sort<benchmark_kind>(
                d_temporary_storage, temporary_storage_bytes,
                d_keys_input, d_keys_output, d_values_input, d_values_output, size,
                stream
            )
        );
    }
    HIP_CHECK(hipDeviceSynchronize());

    const unsigned int batch_size = 10;
    for(auto _ : state)
    {
        auto start = std::chrono::high_resolution_clock::now();
        for(size_t i = 0; i < batch_size; i++)
        {
            HIP_CHECK(
                run_device_radix_sort<benchmark_kind>(
                    d_temporary_storage, temporary_storage_bytes,
                    d_keys_input, d_keys_output, d_values_input, d_values_output, size,
                    stream
                )
            );
        }
        HIP_CHECK(hipStreamSynchronize(stream));

        auto end = std::chrono::high_resolution_clock::now();
        auto elapsed_seconds =
            std::chrono::duration_cast<std::chrono::duration<double>>(end - start);
        state.SetIterationTime(elapsed_seconds.count());
    }
    state.SetBytesProcessed(state.iterations() * batch_size * size * sizeof(key_type));
    state.SetItemsProcessed(state.iterations() * batch_size * size);

    HIP_CHECK(hipFree(d_temporary_storage));
    HIP_CHECK(hipFree(d_keys_input));
    HIP_CHECK(hipFree(d_keys_output));
    HIP_CHECK(hipFree(d_values_input));
    HIP_CHECK(hipFree(d_values_output));
}

#define CREATE_BENCHMARK(T) \
benchmark::RegisterBenchmark( \
    (std::string("device_radix_") + name + "<" #T ">").c_str(), \
    run_benchmark<benchmark_kind, T>, \
    stream, size \
)

template<benchmark_kinds benchmark_kind>
void add_benchmarks(const std::string& name,
                    std::vector<benchmark::internal::Benchmark*>& benchmarks,
                    hipStream_t stream,
                    size_t size)
{
    std::vector<benchmark::internal::Benchmark*> bs =
    {
        CREATE_BENCHMARK(unsigned int),
        CREATE_BENCHMARK(int),

        CREATE_BENCHMARK(unsigned long long),
        CREATE_BENCHMARK(long long),

        CREATE_BENCHMARK(char),
        CREATE_BENCHMARK(short),

        CREATE_BENCHMARK(float),
        CREATE_BENCHMARK(double),
    };

    benchmarks.insert(benchmarks.end(), bs.begin(), bs.end());
}

int main(int argc, char *argv[])
{
    cli::Parser parser(argc, argv);
    parser.set_optional<size_t>("size", "size", DEFAULT_N, "number of values");
    parser.set_optional<int>("trials", "trials", -1, "number of iterations");
    parser.run_and_exit_if_error();

    // Parse argv
    benchmark::Initialize(&argc, argv);
    const size_t size = parser.get<size_t>("size");
    const int trials = parser.get<int>("trials");

    // HIP
    hipStream_t stream = 0; // default
    hipDeviceProp_t devProp;
    int device_id = 0;
    HIP_CHECK(hipGetDevice(&device_id));
    HIP_CHECK(hipGetDeviceProperties(&devProp, device_id));
    std::cout << "[HIP] Device name: " << devProp.name << std::endl;

    // Add benchmarks
    std::vector<benchmark::internal::Benchmark*> benchmarks;
    add_benchmarks<benchmark_kinds::sort_keys>("sort_keys", benchmarks, stream, size);
    add_benchmarks<benchmark_kinds::sort_keys_desc>("sort_keys_desc", benchmarks, stream, size);
    add_benchmarks<benchmark_kinds::sort_pairs>("sort_pairs", benchmarks, stream, size);
    add_benchmarks<benchmark_kinds::sort_pairs_desc>("sort_pairs_desc", benchmarks, stream, size);

    // Use manual timing
    for(auto& b : benchmarks)
    {
        b->UseManualTime();
        b->Unit(benchmark::kMillisecond);
    }

    // Force number of iterations
    if(trials > 0)
    {
        for(auto& b : benchmarks)
        {
            b->Iterations(trials);
        }
    }

    // Run benchmarks
    benchmark::RunSpecifiedBenchmarks();
    return 0;
}
