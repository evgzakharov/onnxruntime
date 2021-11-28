// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/providers/cuda/math/softmax.h"

#include "core/providers/common.h"
#include "core/providers/cuda/cudnn_common.h"
#include "core/providers/cuda/shared_inc/accumulation_type.h"
#include "core/providers/cuda/tensor/transpose.h"
#include "core/framework/random_generator.h"

namespace onnxruntime {
namespace cuda {

template <typename T, bool is_log_softmax>
Status SoftMaxComputeHelper(
    cudaStream_t stream,
    const T* X,
    const TensorShape& input_shape,
    T* Y,
    int64_t axis) {
  typedef typename ToCudaType<T>::MappedType CudaT;

  int64_t N = input_shape.SizeToDimension(axis);
  int64_t D = input_shape.SizeFromDimension(axis);
  auto Y_data = reinterpret_cast<CudaT*>(Y);
  auto X_data = reinterpret_cast<const CudaT*>(X);

  if (D <= 1024 && D * sizeof(T) <= 4096) {
    dispatch_warpwise_softmax_forward<CudaT, CudaT, AccumulationType_t<CudaT>, is_log_softmax, false, false>(
      stream, Y_data, X_data, gsl::narrow_cast<int>(D),  gsl::narrow_cast<int>(D), gsl::narrow_cast<int>(N));
  } else {
    dispatch_blockwise_softmax_forward<CudaT, CudaT, AccumulationType_t<CudaT>, is_log_softmax>(
      stream, Y_data, X_data, gsl::narrow_cast<int>(D), gsl::narrow_cast<int>(D), gsl::narrow_cast<int>(N));
  }

  return Status::OK();
}

#define SPECIALIZED_SOFTMAX_HELPER_IMPL(T)                                                                                                                 \
  template Status SoftMaxComputeHelper<T, false>(cudaStream_t stream, const T* input, const TensorShape& shape, T* Y, int64_t axis); \
  template Status SoftMaxComputeHelper<T, true>(cudaStream_t stream, const T* input, const TensorShape& shape, T* Y, int64_t axis);

SPECIALIZED_SOFTMAX_HELPER_IMPL(float)
SPECIALIZED_SOFTMAX_HELPER_IMPL(double)
SPECIALIZED_SOFTMAX_HELPER_IMPL(MLFloat16)

#if defined(CUDA_VERSION) && CUDA_VERSION >= 11000
// cudnnSoftmaxForward/Backward doesn't support BFloat16.
#define SPECIALIZED_SOFTMAX_HELPER_IMPL_BFloat16(is_log_softmax)                                               \
  template <>                                                                                                  \
  Status SoftMaxComputeHelper<BFloat16, is_log_softmax>(                                                       \
      cudaStream_t stream,                                                                                     \
      const BFloat16* X,                                                                                       \
      const TensorShape& input_shape,                                                                          \
      BFloat16* Y,                                                                                             \
      int64_t axis) {                                                                                          \
    typedef typename ToCudaType<BFloat16>::MappedType CudaT;                                                   \
    int64_t N = input_shape.SizeToDimension(axis);                                                             \
    int64_t D = input_shape.SizeFromDimension(axis);                                                           \
    auto Y_data = reinterpret_cast<CudaT*>(Y);                                                                 \
    auto X_data = reinterpret_cast<const CudaT*>(X);                                                           \
    dispatch_warpwise_softmax_forward<CudaT, CudaT, AccumulationType_t<CudaT>, is_log_softmax, false, false>(                         \
        stream, Y_data, X_data, gsl::narrow_cast<int>(D), gsl::narrow_cast<int>(D), gsl::narrow_cast<int>(N)); \
    return Status::OK();                                                                                       \
  }

SPECIALIZED_SOFTMAX_HELPER_IMPL_BFloat16(true)
    SPECIALIZED_SOFTMAX_HELPER_IMPL_BFloat16(false)
#endif

#define REGISTER_KERNEL_TYPED(T)                                                           \
  ONNX_OPERATOR_VERSIONED_TYPED_KERNEL_EX(                                                 \
      Softmax,                                                                             \
      kOnnxDomain,                                                                         \
      1, 10,                                                                               \
      T,                                                                                   \
      kCudaExecutionProvider,                                                              \
      (*KernelDefBuilder::Create()).TypeConstraint("T", DataTypeImpl::GetTensorType<T>()), \
      Softmax<T>);                                                                         \
  ONNX_OPERATOR_VERSIONED_TYPED_KERNEL_EX(                                                 \
      Softmax,                                                                             \
      kOnnxDomain,                                                                         \
      11, 12,                                                                              \
      T,                                                                                   \
      kCudaExecutionProvider,                                                              \
      (*KernelDefBuilder::Create()).TypeConstraint("T", DataTypeImpl::GetTensorType<T>()), \
      Softmax<T>);                                                                         \
  ONNX_OPERATOR_TYPED_KERNEL_EX(                                                           \
      Softmax,                                                                             \
      kOnnxDomain,                                                                         \
      13,                                                                                  \
      T,                                                                                   \
      kCudaExecutionProvider,                                                              \
      (*KernelDefBuilder::Create()).TypeConstraint("T", DataTypeImpl::GetTensorType<T>()), \
      Softmax<T>);                                                                         \
  ONNX_OPERATOR_VERSIONED_TYPED_KERNEL_EX(                                                 \
      LogSoftmax,                                                                          \
      kOnnxDomain,                                                                         \
      1, 10,                                                                               \
      T,                                                                                   \
      kCudaExecutionProvider,                                                              \
      (*KernelDefBuilder::Create()).TypeConstraint("T", DataTypeImpl::GetTensorType<T>()), \
      Softmax<T>);                                                                         \
  ONNX_OPERATOR_VERSIONED_TYPED_KERNEL_EX(                                                 \
      LogSoftmax,                                                                          \
      kOnnxDomain,                                                                         \
      11, 12,                                                                              \
      T,                                                                                   \
      kCudaExecutionProvider,                                                              \
      (*KernelDefBuilder::Create()).TypeConstraint("T", DataTypeImpl::GetTensorType<T>()), \
      Softmax<T>);                                                                         \
  ONNX_OPERATOR_TYPED_KERNEL_EX(                                                           \
      LogSoftmax,                                                                          \
      kOnnxDomain,                                                                         \
      13,                                                                                  \
      T,                                                                                   \
      kCudaExecutionProvider,                                                              \
      (*KernelDefBuilder::Create()).TypeConstraint("T", DataTypeImpl::GetTensorType<T>()), \
      Softmax<T>);

template <typename T>
Status Softmax<T>::ComputeInternal(OpKernelContext* ctx) const {
  const Tensor* X = ctx->Input<Tensor>(0);
  const TensorShape& input_shape{X->Shape()};
  size_t rank = input_shape.NumDimensions();
  Tensor* Y = ctx->Output(0, input_shape);

  // special case when there is a dim value of 0 in the shape.
  if (input_shape.Size() == 0)
    return Status::OK();

  // handle negative and enforce axis is valid
  const size_t axis = static_cast<size_t>(HandleNegativeAxis(axis_, rank));

  bool is_transpose_required = false;
  std::unique_ptr<Tensor> transposed_input;
  std::vector<int64_t> transposed_input_dims;
  std::unique_ptr<Tensor> intermediate_output;  // output that the softmax implementation will write into while using transposed input
  std::vector<size_t> permutation(rank);

  // The "semantic" meaning of axis has changed in opset-13.
  // Please compare: https://github.com/onnx/onnx/blob/master/docs/Operators.md#Softmax
  // with https://github.com/onnx/onnx/blob/master/docs/Changelog.md#Softmax-11 for detailed explanations
  // To account for the opset-13 behavior, our plan will be to transpose the "axis" dim to the innermost dim
  // and perform softmax and then reverse the transpose. We can skip the transposing aspect if the axis is already
  // the innermost dim
  if (opset_ >= 13 && axis != (rank - 1)) {
    is_transpose_required = true;
  }

  if (is_transpose_required) {
    AllocatorPtr alloc;
    auto status = ctx->GetTempSpaceAllocator(&alloc);
    if (!status.IsOK())
      return status;

    std::iota(std::begin(permutation), std::end(permutation), 0);

    // swap the innermost dim with the dim corresponding to axis
    permutation[axis] = rank - 1;
    permutation[rank - 1] = axis;

    transposed_input_dims.reserve(rank);
    for (auto e : permutation) {
      transposed_input_dims.push_back(input_shape[e]);
    }

    // Allocate a temporary tensor to hold transposed input
    auto temp_input = Tensor::Create(X->DataType(), TensorShape(transposed_input_dims), alloc);

    // Perform the transpose
    ORT_RETURN_IF_ERROR(Transpose::DoTranspose(cuda_ep_->GetDeviceProp(),
                                               Stream(),
                                               CublasHandle(),
                                               permutation, *X, *temp_input));
    transposed_input = std::move(temp_input);

    // Allocate memory for the intermediate output
    intermediate_output = Tensor::Create(Y->DataType(), TensorShape(transposed_input_dims), alloc);
  }

  const T* X_data = nullptr;
  T* Y_data = nullptr;
  const TensorShape* compute_input_shape = nullptr;

  if (is_transpose_required) {  // use intermediate buffers to compute the softmax values
    X_data = transposed_input->template Data<T>();
    Y_data = intermediate_output->template MutableData<T>();
    compute_input_shape = &transposed_input->Shape();
  } else {  // use the node input/output directly
    X_data = X->template Data<T>();
    Y_data = Y->template MutableData<T>();
    compute_input_shape = &input_shape;
  }

  Status status;
  if (log_softmax_) {
    status = SoftMaxComputeHelper<T, true>(Stream(), X_data, *compute_input_shape, Y_data,
                                           is_transpose_required ? static_cast<int64_t>(rank) - 1
                                                                 : static_cast<int64_t>(axis));
  } else {
    status = SoftMaxComputeHelper<T, false>(Stream(), X_data, *compute_input_shape, Y_data,
                                            is_transpose_required ? static_cast<int64_t>(rank) - 1
                                                                  : static_cast<int64_t>(axis));
  }

  if (!status.IsOK())
    return status;

  if (is_transpose_required) {
    // Perform the transpose to get the axes back to the original ordering
    ORT_RETURN_IF_ERROR(Transpose::DoTranspose(cuda_ep_->GetDeviceProp(),
                                               Stream(),
                                               CublasHandle(),
                                               permutation, *intermediate_output, *Y));
  }

  return Status::OK();
}

#define SPECIALIZED_COMPUTE(T) \
  REGISTER_KERNEL_TYPED(T)     \
  template Status Softmax<T>::ComputeInternal(OpKernelContext* ctx) const;

SPECIALIZED_COMPUTE(float)
SPECIALIZED_COMPUTE(double)
SPECIALIZED_COMPUTE(MLFloat16)
#if defined(CUDA_VERSION) && CUDA_VERSION >= 11000
SPECIALIZED_COMPUTE(BFloat16)
#endif


template <typename T, bool is_log_softmax, typename T2>
Status FusedSoftMaxComputeHelper(
    cudaStream_t stream,
    const T* X,
    const TensorShape& input_shape,
    T* Y,
    int64_t axis,
    PhiloxGenerator* generator,
    float dropout_ratio,
    T* dropout_result,
    T2* dropout_mask) {
  typedef typename ToCudaType<T>::MappedType CudaT;

  int64_t N = input_shape.SizeToDimension(axis);
  int64_t D = input_shape.SizeFromDimension(axis);
  auto Y_data = reinterpret_cast<CudaT*>(Y);
  auto X_data = reinterpret_cast<const CudaT*>(X);

  if (D <= 1024 && D * sizeof(T) <= 4096) {
    dispatch_warpwise_softmax_forward<CudaT, CudaT, AccumulationType_t<CudaT>, is_log_softmax, false, true>(
      stream, Y_data, X_data, gsl::narrow_cast<int>(D),  gsl::narrow_cast<int>(D), gsl::narrow_cast<int>(N),
      generator, dropout_ratio, reinterpret_cast<CudaT*>(dropout_result), reinterpret_cast<void *>(dropout_mask)
      );
  } else {
    dispatch_blockwise_softmax_forward<CudaT, CudaT, AccumulationType_t<CudaT>, is_log_softmax>(
      stream, Y_data, X_data, gsl::narrow_cast<int>(D), gsl::narrow_cast<int>(D), gsl::narrow_cast<int>(N));
  }

  return Status::OK();
}

#define SPECIALIZED_FUSED_SOFTMAX_HELPER_IMPL(T, T2)                                                                                                                 \
  template Status FusedSoftMaxComputeHelper<T, false, T2>(cudaStream_t stream, const T* input, const TensorShape& shape, T* Y, int64_t axis, PhiloxGenerator* generator, float dropout_ratio, T* dropout_result, T2* dropout_mask); \
  template Status FusedSoftMaxComputeHelper<T, true, T2>(cudaStream_t stream, const T* input, const TensorShape& shape, T* Y, int64_t axis, PhiloxGenerator* generator, float dropout_ratio, T* dropout_result, T2* dropout_mask);

SPECIALIZED_FUSED_SOFTMAX_HELPER_IMPL(float, uint8_t)
SPECIALIZED_FUSED_SOFTMAX_HELPER_IMPL(double, uint8_t)
SPECIALIZED_FUSED_SOFTMAX_HELPER_IMPL(MLFloat16, uint8_t)
SPECIALIZED_FUSED_SOFTMAX_HELPER_IMPL(float, uint16_t)
SPECIALIZED_FUSED_SOFTMAX_HELPER_IMPL(double, uint16_t)
SPECIALIZED_FUSED_SOFTMAX_HELPER_IMPL(MLFloat16, uint16_t)
SPECIALIZED_FUSED_SOFTMAX_HELPER_IMPL(float, uint32_t)
SPECIALIZED_FUSED_SOFTMAX_HELPER_IMPL(double, uint32_t)
SPECIALIZED_FUSED_SOFTMAX_HELPER_IMPL(MLFloat16, uint32_t)
SPECIALIZED_FUSED_SOFTMAX_HELPER_IMPL(float, uint64_t)
SPECIALIZED_FUSED_SOFTMAX_HELPER_IMPL(double, uint64_t)
SPECIALIZED_FUSED_SOFTMAX_HELPER_IMPL(MLFloat16, uint64_t)

template <typename T, typename T1, typename T2>
Status AdditiveMaskSoftmaxDropout<T, T1, T2>::ComputeInternal(OpKernelContext* ctx) const {
  const Tensor* X = ctx->Input<Tensor>(0);
  const TensorShape& input_shape{X->Shape()};
  const int64_t N = input_shape.Size();

  size_t rank = input_shape.NumDimensions();
  Tensor* Y = ctx->Output(0, input_shape);

  // special case when there is a dim value of 0 in the shape.
  if (input_shape.Size() == 0)
    return Status::OK();

  // handle negative and enforce axis is valid
  // const size_t axis = static_cast<size_t>(HandleNegativeAxis(axis_, rank));

  const size_t axis = rank - 1;

  const T* X_data = nullptr;
  T* Y_data = nullptr;
  const TensorShape* compute_input_shape = nullptr;

  // use the node input/output directly
  X_data = X->template Data<T>();
  Y_data = Y->template MutableData<T>();
  compute_input_shape = &input_shape;


  auto mask = ctx->Output(2, input_shape);
  auto dropout_result = ctx->Output(1, input_shape);
  ORT_ENFORCE(!mask || mask->Shape().Size() == N);
  //Get the ratio_data
  float ratio_data = 1.0f; //default_ratio_;
  // auto ratio = context->Input<Tensor>(2);
  // if (ratio) {
  //   utils::MLTypeCallDispatcher<ALL_IEEE_FLOAT_DATA_TYPES> t_disp(ratio->GetElementType());
  //   t_disp.Invoke<GetRatioDataImpl>(ratio, ratio_data);
  // }



PhiloxGenerator& generator = generator_ ? *generator_ : PhiloxGenerator::Default();
  Status status;
  if (log_softmax_) {
    status = FusedSoftMaxComputeHelper<T, true>(Stream(), X_data, *compute_input_shape, Y_data, static_cast<int64_t>(axis),
      &generator, ratio_data, dropout_result->template MutableData<T>(), mask->MutableData<T2>());
  } else {
    status = FusedSoftMaxComputeHelper<T, false>(Stream(), X_data, *compute_input_shape, Y_data, static_cast<int64_t>(axis),
    &generator, ratio_data, dropout_result->template MutableData<T>(), mask->MutableData<T2>());
  }

  if (!status.IsOK())
    return status;

  return Status::OK();
}


#define REGISTER_FUSED_KERNEL_TYPED(T, T1, T2)                                                           \
  ONNX_OPERATOR_TYPED_KERNEL_EX(                                                           \
      AdditiveMaskSoftmaxDropout,                                                                             \
      kMSDomain,                                                                         \
      1,                                                                                  \
      T##_##T1##_##T2,                                              \
      kCudaExecutionProvider,                                                              \
      (*KernelDefBuilder::Create()) \
        .TypeConstraint("T", DataTypeImpl::GetTensorType<T>()) \
          .TypeConstraint("T1", DataTypeImpl::GetTensorType<T1>())  \
          .TypeConstraint("T2", DataTypeImpl::GetTensorType<T2>()), \
      AdditiveMaskSoftmaxDropout<T, T1, T2>);

#define SPECIALIZED_FUSED_COMPUTE(T, T1, T2) \
  REGISTER_FUSED_KERNEL_TYPED(T, T1, T2)     \
  template Status AdditiveMaskSoftmaxDropout<T, T1, T2>::ComputeInternal(OpKernelContext* ctx) const;

SPECIALIZED_FUSED_COMPUTE(float, float, uint8_t)
SPECIALIZED_FUSED_COMPUTE(double, float, uint8_t)
SPECIALIZED_FUSED_COMPUTE(MLFloat16, float, uint8_t)
SPECIALIZED_FUSED_COMPUTE(float, float, uint16_t)
SPECIALIZED_FUSED_COMPUTE(double, float, uint16_t)
SPECIALIZED_FUSED_COMPUTE(MLFloat16, float, uint16_t)
SPECIALIZED_FUSED_COMPUTE(float, float, uint32_t)
SPECIALIZED_FUSED_COMPUTE(double, float, uint32_t)
SPECIALIZED_FUSED_COMPUTE(MLFloat16, float, uint32_t)
SPECIALIZED_FUSED_COMPUTE(float, float, uint64_t)
SPECIALIZED_FUSED_COMPUTE(double, float, uint64_t)
SPECIALIZED_FUSED_COMPUTE(MLFloat16, float, uint64_t)

}  // namespace cuda
}  // namespace onnxruntime