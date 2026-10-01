// SPDX-License-Identifier: MIT
// Private investigation hooks, compiled only into the benchmark's static backend.
#pragma once

#include "pyrowave_metal.h"

struct pyrowave_bench_decode_timings
{
	double upload_cpu_ms;
	double dequant_encode_cpu_ms;
	double idwt_encode_cpu_ms;
	double dequant_gpu_ms;
	double idwt_gpu_ms;
};

struct pyrowave_bench_encode_diagnostics
{
	uint32_t coefficient_payload_bytes;
	uint32_t bitstream_payload_words;
	uint64_t coefficient_payload_capacity_bytes;
	uint64_t bitstream_capacity_bytes;
};

extern "C" double pyrowave_bench_last_gpu_ms(pyrowave_encoder encoder);
// Blocks for encoding and a separate counter readback. Call outside measurements.
extern "C" pyrowave_result pyrowave_bench_get_encode_diagnostics(
		pyrowave_encoder encoder, pyrowave_bench_encode_diagnostics *diagnostics);
extern "C" pyrowave_result pyrowave_bench_set_batched_dequant(pyrowave_decoder decoder, bool enabled);
extern "C" pyrowave_result pyrowave_bench_set_reduced_idwt_barriers(pyrowave_decoder decoder, bool enabled);
extern "C" pyrowave_result pyrowave_bench_set_decode_profiling(pyrowave_decoder decoder, bool enabled);
// Call only after the decode command buffer completes.
extern "C" pyrowave_result pyrowave_bench_get_decode_timings(
		pyrowave_decoder decoder, pyrowave_bench_decode_timings *timings);
