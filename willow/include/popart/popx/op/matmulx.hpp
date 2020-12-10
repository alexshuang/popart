// Copyright (c) 2018 Graphcore Ltd. All rights reserved.
#ifndef GUARD_NEURALNET_MATMULX_HPP
#define GUARD_NEURALNET_MATMULX_HPP

#include <popart/names.hpp>
#include <popart/popx/opx.hpp>
#include <popart/popx/poplaroptionsx.hpp>

namespace popart {
namespace popx {

class MatMulOpx : public Opx {
public:
  MatMulOpx(Op *, Devicex *);
  ~MatMulOpx() override = default;

  poplar::Tensor createInput(InIndex index,
                             const std::string &name) const final;
  InputCreatorType getInputCreatorType(InIndex index) const final;
  bool createsEquiv(int, const Opx *, int) const final;
  std::vector<TensorId> mustExistBeforeCreate(InIndex index0) const final;

  MatMulOp *getMatMulOp() const;
  void grow(poplar::program::Sequence &) const final;

  poplar::Type getOutputType(const poplar::Tensor &output) const;
  static std::vector<std::size_t> onnxShapeToPoplar(const Shape &shape);
  static PoplarOptions getPoplarOptionsForMatMul(const MatMulBaseOp &op);
  static void addPartialsType(const MatMulPartialsType &partialsType,
                              poplar::OptionFlags &opts);
  static void setMatMulOptions(const MatMulBaseOp &op,
                               poplar::OptionFlags &opts);

  static std::pair<poplar::Tensor, poplar::Tensor>
  groupedMatMulInputsFromOpxInputs(MatMulBaseOp &matmul,
                                   poplar::Tensor lhs,
                                   poplar::Tensor rhs);

private:
  // The ONNX tensor shape
  std::vector<std::size_t> getOutputShape() const;
};

} // namespace popx
} // namespace popart

#endif
