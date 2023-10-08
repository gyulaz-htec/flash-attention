// BSD 3 Clause
// Copyright 2023 Advanced Micro Devices, Inc.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 1. Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
// 3. Neither the name of the copyright holder nor the names of its contributors
// may be used to endorse or promote products derived from this software without
// specific prior written permission. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
// HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
// INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
// FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
// COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
// OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
// EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#pragma once

#include "bwd_device_gemm_template.h"
#include "params.h"

namespace bwd_device_gemm {
template <template <typename> typename DeviceGemmTemplate, typename DeviceGemmTraits>
class DeviceGemmInstanceLauncher {
 public:
  // constructor
  explicit DeviceGemmInstanceLauncher(FlashBwdParams &params, hipStream_t &stream)
    : b_(params.b),
      h_(params.h),
      d_(params.d),
      scale_softmax_(params.scale_softmax),
      q_ptr_(params.q_ptr),
      k_ptr_(params.k_ptr),
      z_ptr_(params.z_ptr),
      v_ptr_(params.v_ptr),
      out_ptr_(params.out_ptr),
      softmax_lse_ptr_(params.softmax_lse_ptr),
      d_ptr_(params.d_ptr),
      dout_ptr_(params.dout_ptr),
      dq_ptr_(params.dq_ptr),
      dk_ptr_(params.dk_ptr),
      dv_ptr_(params.dv_ptr),
      a_gs_ms_ks_lengths_(params.a_gs_ms_ks_lengths),
      a_gs_ms_ks_strides_(params.a_gs_ms_ks_strides),
      b_gs_ns_ks_lengths_(params.b_gs_ns_ks_lengths),
      b_gs_ns_ks_strides_(params.b_gs_ns_ks_strides),
      b1_gs_gemm1ns_gemm1ks_lengths_(params.b1_gs_gemm1ns_gemm1ks_lengths),
      b1_gs_gemm1ns_gemm1ks_strides_(params.b1_gs_gemm1ns_gemm1ks_strides),
      c_gs_ms_gemm1ns_lengths_(params.c_gs_ms_gemm1ns_lengths), 
      c_gs_ms_gemm1ns_strides_(params.c_gs_ms_gemm1ns_strides), 
      z_gs_ms_ns_lengths_(params.z_gs_ms_ns_lengths),
      z_gs_ms_ns_strides_(params.z_gs_ms_ns_strides),
      lse_gs_ms_lengths_(params.lse_gs_ms_lengths),
      acc0_biases_gs_ms_ns_lengths_(params.acc0_biases_gs_ms_ns_lengths),
      acc0_biases_gs_ms_ns_strides_(params.acc0_biases_gs_ms_ns_strides),
      p_dropout_(params.p_dropout),
      seeds_(params.seeds),
      device_gemm_instance_ptr_(std::make_unique<DeviceGemmTemplate<DeviceGemmTraits>>()),
      stream_(stream) {
    
  }

 private:
  int b_;
  int h_;
  int d_;
  float scale_softmax_;

  const void* q_ptr_;
  const void* k_ptr_;
  void* z_ptr_;
  const void* v_ptr_;
  const void* out_ptr_; 
  const void* softmax_lse_ptr_;
  void* d_ptr_;
  const void* dout_ptr_;
  void* dq_ptr_;
  void* dk_ptr_;
  void* dv_ptr_;
  std::vector<Index> a_gs_ms_ks_lengths_;
  std::vector<Index> a_gs_ms_ks_strides_;
  std::vector<Index> b_gs_ns_ks_lengths_;
  std::vector<Index> b_gs_ns_ks_strides_;
  std::vector<Index> b1_gs_gemm1ns_gemm1ks_lengths_; // b1_gs_os_ns_lengths
  std::vector<Index> b1_gs_gemm1ns_gemm1ks_strides_; // b1_gs_os_ns_strides
  std::vector<Index> c_gs_ms_gemm1ns_lengths_;       // c_gs_ms_os_lengths
  std::vector<Index> c_gs_ms_gemm1ns_strides_;       // c_gs_ms_os_strides
  std::vector<Index> z_gs_ms_ns_lengths_;
  std::vector<Index> z_gs_ms_ns_strides_;
  std::vector<Index> lse_gs_ms_lengths_;
  std::vector<Index> acc0_biases_gs_ms_ns_lengths_;
  std::vector<Index> acc0_biases_gs_ms_ns_strides_;
  float p_dropout_;
  std::tuple<uint64_t, uint64_t> seeds_;

  std::unique_ptr<DeviceGemmTemplate<DeviceGemmTraits>> device_gemm_instance_ptr_;
  hipStream_t &stream_;

  static constexpr bool kIsBatched = std::is_same_v<DeviceGemmTemplate, DeviceGemmGroupedHeadDim32> || 
                                     std::is_same_v<DeviceGemmTemplate, DeviceGemmGroupedHeadDim64> ||
                                     std::is_same_v<DeviceGemmTemplate, DeviceGemmGroupedHeadDim128>;

  static const bool time_kernel = false;
  static const bool input_permute = true;
  static const bool output_permute = true;
}; // class BwdDeviceGemmInstanceLauncher

template <template <typename> typename DeviceGemmTemplate, typename DeviceGemmTraits>
void DeviceGemmInstanceLauncher<DeviceGemmTemplate, DeviceGemmTraits>::Launch() {
  for (int i = 0; i < batch_size; ++i) {
    int M = params.host_seqlens_q[i + 1] - params.host_seqlens_q[i]; // seqlen Q
    int N = params.host_seqlens_k[i + 1] - params.host_seqlens_k[i]; // seqlen K
    int K = head_dim;
    int O = head_dim;
    int G0 = 1; // G0 = batch_size
    int G1 = num_heads;

    std::vector<ck::index_t> q_gs_ms_ks_lengths{G0, G1, M, K};
    std::vector<ck::index_t> q_gs_ms_ks_strides =
        input_permute
            ? std::vector<ck::index_t>{M * G1 * K * params.q_stride_multiplier, K, G1 * K * params.q_stride_multiplier, 1}
            // Q layout [G0, M, G1, K]
            : std::vector<ck::index_t>{G1 * M * K, M * K, K, 1}; // Q layout [G0, G1, M, K]

    std::vector<ck::index_t> k_gs_ns_ks_lengths{G0, G1, N, K};
    std::vector<ck::index_t> k_gs_ns_ks_strides =
        input_permute
            ? std::vector<ck::index_t>{N * G1 * K * params.kv_stride_multiplier, K, G1 * K * params.kv_stride_multiplier, 1}
            // K layout [G0, N, G1, K]
            : std::vector<ck::index_t>{G1 * N * K, N * K, K, 1}; // K layout [G0, G1, N, K]

    std::vector<ck::index_t> v_gs_os_ns_lengths{G0, G1, O, N};
    std::vector<ck::index_t> v_gs_os_ns_strides =
        input_permute
            ? std::vector<ck::index_t>{N * G1 * O * params.kv_stride_multiplier, O, 1, G1 * O * params.kv_stride_multiplier}
            // V layout [G0, N, G1, O]
            : std::vector<ck::index_t>{G1 * N * O, N * O, 1, O}; // V layout [G0, G1, N, O]

    std::vector<ck::index_t> y_gs_ms_os_lengths{G0, G1, M, O};
    std::vector<ck::index_t> y_gs_ms_os_strides =
        output_permute
            ? std::vector<ck::index_t>{M * G1 * O, O, G1 * O, 1}
            // Y layout [G0, M, G1, O]
            : std::vector<ck::index_t>{G1 * M * O, M * O, O, 1}; // Y layout [G0, G1, M, O]

    std::vector<ck::index_t> z_gs_ms_ns_lengths{G0, G1, M, N};
    std::vector<ck::index_t> z_gs_ms_ns_strides = 
        input_permute
        ? std::vector<ck::index_t>{M * G1 * N, N, G1 * N, 1}
        // Z layout [G0, M, G1, N]
        : std::vector<ck::index_t>{G1 * M * N, M * N, N, 1}; // Z layout [G0, G1, M, N]
    // The softmax stat log-sum-exp (LSE) is used to speed up softmax
    // calculation in backward pass Pi = exp(Si) / sum(exp(S0) + exp(S1) +
    // ...)
    //    = exp(Si) / exp(log(sum(exp() + ...)))
    //    = exp(Si - log(sum(exp() + ...)))
    //               ^^^^^^^^^^^^^^^^^^^^^
    //                       LSE
    std::vector<ck::index_t> lse_gs_ms_lengths{G0, G1, M};
    std::vector<ck::index_t> lse_gs_ms_strides{G1 * M, M, 1}; // LSE layout [G0, G1, M]


    std::vector<typename DeviceGemmTemplate::ProblemDesc> problem_descs_;
    problem_descs.push_back({
        q_gs_ms_ks_lengths,
        q_gs_ms_ks_strides,
        k_gs_ns_ks_lengths,
        k_gs_ns_ks_strides,
        z_gs_ms_ns_lengths,
        z_gs_ms_ns_strides,
        v_gs_os_ns_lengths,
        v_gs_os_ns_strides,
        y_gs_ms_os_lengths,
        y_gs_ms_os_strides,
        lse_gs_ms_lengths,
        lse_gs_ms_strides,
        {}, // acc0_biases_gs_ms_ns_lengths
        {}, // acc0_biases_gs_ms_ns_strides
        {}, // acc1_biases_gs_ms_os_lengths
        {}  // acc1_biases_gs_ms_os_strides
    });
  }

  auto invoker = device_gemm_instance_ptr_->MakeInvoker();
  auto argument = device_gemm_instance_ptr_->MakeArgument(
      p_q,
      p_k,
      p_z,
      p_v,
      p_y,
      p_lse,
      p_d,
      p_ygrad, 
      p_qgrad,
      p_kgrad,
      p_vgrad,
      {},
      {}, 
      device_gemm_trait::AElementOp{};
      device_gemm_trait::B0ElementOp{};
      device_gemm_trait::Acc0ElementOp{scale_softmax_};
      device_gemm_trait::B1ElementOp{};
      device_gemm_trait::CElementOp{};  
      dropout_ratio, 
      seeds);

  // specify workspace for problem_desc
  SimpleDeviceMem problem_desc_workspace{ device_gemm_instance_ptr_->GetWorkSpaceSize(&argument) };

  device_gemm_instance_ptr_->SetWorkSpacePointer(&argument,
                          problem_desc_workspace.GetDeviceBuffer());

  if (!device_gemm_instance_ptr_->IsSupportedArgument(argument)) {
    std::cout << device_gemm_instance_ptr_->GetTypeString() << " does not support this problem"
              << std::endl;
    return;
  }

  float avg_time = invoker.Run(argument, StreamConfig{stream_, time_kernel});

  if (time_kernel) {
    std::cout << "time elpase is " << avg_time << " ms" << std::endl;
  }
} // end of function Launch
} // namespace bwd_device_gemm