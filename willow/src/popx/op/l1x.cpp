// Copyright (c) 2018 Graphcore Ltd. All rights reserved.
#include <cstddef>
#include <snap/Graph.hpp>
#include <snap/Program.hpp>
#include <snap/Tensor.hpp>
#include <snap/popops/ElementWise.hpp>
#include <vector>
#include <poplar/Type.hpp>
#include <popops/ElementWise.hpp>
#include <popops/Expr.hpp>
#include <popops/ExprOp.hpp>
#include <popops/OperationDef.hpp>
#include <popops/Reduce.hpp>
#include <popart/error.hpp>
#include <popart/op/l1.hpp>
#include <popart/popx/op/l1x.hpp>
#include <popart/popx/opxmanager.hpp>

#include "popart/graphcoreoperators.hpp"
#include "popart/logging.hpp"
#include "popart/names.hpp"
#include "popart/op.hpp"
#include "popart/popx/debugcontextx.hpp"
#include "popart/popx/popopx.hpp"
#include "popart/region.hpp" // IWYU pragma: keep

namespace pe = popops::expr;

namespace popart {

namespace popx {
class Devicex;

L1Opx::L1Opx(Op *op, Devicex *devicex) : PopOpx(op, devicex) {
  verifyOp<L1Op>(op, Onnx::CustomOperators::L1);
}

void L1GradOpx::grow(snap::program::Sequence &prog) const {
  L1GradOp &l1gradop = getOp<L1GradOp>();

  double lambda = static_cast<double>(l1gradop.getLambda());

  // Signum : +1 of positive, -1 if negative, 0 if zero.
  auto signumTensor = snap::Tensor{
      popops::map(graph().getPoplarGraph(),
                  popops::expr::UnaryOpType::SIGNUM,
                  getInTensor(L1GradOp::getFwdActInIndex()).getPoplarTensor(),
                  prog.getPoplarSequence(),
                  debugContext("Signum")),
      graph()};

  double scale = lambda;
  switch (l1gradop.getReductionType()) {
  case ReductionType::NoReduction:
    break;
  case ReductionType::Sum:
    break;
  case ReductionType::Mean: {
    double totalSamples = static_cast<double>(getInTensor(0).numElements());
    scale               = lambda / totalSamples;
    break;
  }
  default:
    throw error("Unsupported reduction type for Loss {}",
                debugContext().getPathName());
  }

  auto t_scale = getConst(getInTensor(0).elementType(), {}, scale, "scale");

  // scale the signum tensor:
  // - first by 'scale',  so +scale if positive, -scale if negative, 0 if zero
  // - by loss scaling factor
  // - then by input gradient

  auto gradTensor = snap::popops::map(graph(),
                                      pe::Mul(pe::_1, pe::_2),
                                      {signumTensor, t_scale},
                                      prog,
                                      debugContext("multiply"));

  auto gradIn = getInTensor(L1GradOp::getGradInIndex());
  snap::popops::mapInPlace(graph(),
                           pe::Mul(pe::_1, pe::_2),
                           {gradTensor, gradIn},
                           prog,
                           debugContext("scaledGradIn"));

  setOutTensor(0, gradTensor);
}

InputCreatorType L1Opx::getInputCreatorType(InIndex index) const {
  if (getOp<L1Op>().getReductionType() == ReductionType::NoReduction &&
      index == L1Op::getInIndex()) {
    return InputCreatorType::CanUnwind;
  }
  return InputCreatorType::Deadend;
}

view::RegMap L1Opx::unwindRegion(InIndex, OutIndex) const {
  return [](const view::Region &r) { return view::Regions(1, r); };
}

snap::Tensor
L1Opx::unwindTensorLayout(snap::Tensor tensor, InIndex, OutIndex) const {
  return tensor;
}

// lambda * sum_{0,..rank-1} |v|
void L1Opx::grow(snap::program::Sequence &prog) const {
  const L1Op &l1op = getOp<L1Op>();
  auto absTensor   = snap::Tensor{popops::map(graph().getPoplarGraph(),
                                            popops::expr::UnaryOpType::ABSOLUTE,
                                            getInTensor(0).getPoplarTensor(),
                                            prog.getPoplarSequence(),
                                            debugContext("abs")),
                                graph()};

  if (absTensor.rank() == 0) {
    throw error("invalid tensor (rank-0) in L1Opx");
  }

  double lambda = static_cast<double>(l1op.getLambda());

  if (l1op.getReductionType() == ReductionType::NoReduction) {
    auto t_scale = getConst(absTensor.elementType(), {}, lambda, "scale");

    auto scaled = snap::popops::map(graph(),
                                    popops::expr::BinaryOpType::MULTIPLY,
                                    absTensor,
                                    t_scale,
                                    prog,
                                    debugContext("add"));
    setOutTensor(0, scaled);
  } else {
    auto absTensor1D = absTensor.flatten();
    double scale     = lambda;

    switch (l1op.getReductionType()) {
    case ReductionType::Sum: {
      break;
    }
    case ReductionType::Mean: {
      double totalSamples = static_cast<double>(absTensor1D.dim(0));
      scale               = lambda / totalSamples;
      break;
    }
    // Making it explicit which data types we're not handling. Note that
    // the logic will fall through to the error.
    case ReductionType::NoReduction:
    default: {
      throw error("Unsupported reduction type for Loss {}",
                  debugContext().getPathName());
    }
    }

    // t_scale is always expected to be FLOAT, regardless of the input type
    // to the reduction
    auto t_scale =
        getConst(poplar::FLOAT, {}, scale, "scale").getPoplarTensor();
    auto reduction = popops::reduce(graph().getPoplarGraph(),
                                    absTensor1D.getPoplarTensor(),
                                    {0},
                                    {popops::Operation::ADD, false, t_scale},
                                    prog.getPoplarSequence(),
                                    debugContext("add"));
    setOutTensor(0, snap::Tensor{reduction, graph()});
  }
}

L1GradOpx::L1GradOpx(Op *op, Devicex *devicex) : PopOpx(op, devicex) {
  verifyOp<L1GradOp>(op, Onnx::CustomGradOperators::L1Grad);
}

namespace {
OpxCreator<L1Opx> l1OpxCreator(Onnx::CustomOperators::L1);
OpxCreator<L1GradOpx> l1GradOpxCreator(Onnx::CustomGradOperators::L1Grad);
} // namespace

} // namespace popx
} // namespace popart
