// Copyright (c) 2021 Graphcore Ltd. All rights reserved.
#ifndef POPART_WILLOW_INCLUDE_POPART_POPX_OP_RNNX_HPP_
#define POPART_WILLOW_INCLUDE_POPART_POPX_OP_RNNX_HPP_

#include <set>
#include <poplar/Program.hpp>
#include <poplar/Tensor.hpp>
#include <poplar/Type.hpp>
#include <popart/names.hpp>
#include <popart/popx/opx.hpp>

#include "popart/popx/debugcontextx.hpp"

namespace popart {
class Op;

namespace popx {
class Devicex;

class RNNOpx : public Opx {
public:
  RNNOpx(Op *, Devicex *);
  void grow(poplar::program::Sequence &) const final;
  InputCreatorType getInputCreatorType(InIndex) const final;
  poplar::Tensor createInput(InIndex index,
                             const poplar::DebugNameAndId &dnai) const final;
  std::set<TensorId> mustExistBeforeCreate(InIndex) const;

private:
  // Return the tensor type of any input tensor (they all have the same type)
  poplar::Type getTensorType() const;
  // Return minimum number of elements of type getTensorType(), so that they
  // take up 16 bytes in total for performance reasons
  unsigned getMinGrainSize() const;
  // Return sum of bias tensors, or 0 if bias tensors not provided by user
  poplar::Tensor getBias(poplar::program::Sequence &) const;
  // Return initialH, or 0 if initialH is not provided by user
  poplar::Tensor getInitialH(poplar::program::Sequence &) const;
  // Return program for a single step of the forward pass
  poplar::program::Sequence getFwdStepProg(poplar::Tensor &bias,
                                           poplar::Tensor &initialH,
                                           poplar::Tensor &output,
                                           poplar::Tensor &H_prev,
                                           poplar::Tensor &index) const;
};

class RNNGradOpx : public Opx {
public:
  RNNGradOpx(Op *, Devicex *);
  void grow(poplar::program::Sequence &) const final;

  InputCreatorType getInputCreatorType(InIndex) const final;
  poplar::Tensor createInput(InIndex index,
                             const poplar::DebugNameAndId &dnai) const final;

  std::set<TensorId> mustExistBeforeCreate(InIndex) const;

private:
  // Return the tensor type of any forward input tensor (they all have the same
  // type)
  poplar::Type getTensorType() const;
  // Return minimum number of elements of type getTensorType(), so that they
  // take up 16 bytes in total for performance reasons
  unsigned getMinGrainSize() const;
  // Return last_output_grad, or 0 if the input does not exist
  poplar::Tensor getLastOutputGrad() const;
  // Return full_output_grad, or 0 if the input does not exist
  poplar::Tensor getFullOutputGrad() const;
  // Return program for a single step of the backwards pass
  poplar::program::Sequence getBwdStepProg(poplar::Tensor &dh_prev,
                                           poplar::Tensor &full_output_grad,
                                           poplar::Tensor &forward_output_prev,
                                           poplar::Tensor &forward_output,
                                           poplar::Tensor &da,
                                           poplar::Tensor &index) const;
};

} // namespace popx
} // namespace popart

#endif // POPART_WILLOW_INCLUDE_POPART_POPX_OP_RNNX_HPP_
