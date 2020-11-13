// Copyright (c) 2018 Graphcore Ltd. All rights reserved.
#include <memory>
#include <popart/graph.hpp>
#include <popart/op/div.hpp>
#include <popart/op/reducesum.hpp>
#include <popart/patterns/divarg0gradoppattern.hpp>
#include <popart/tensor.hpp>
#include <popart/tensorinfo.hpp>

namespace popart {

bool DivArg0GradOpPattern::matches(Op *op) const {
  return op->isConvertibleTo<DivArg0GradOp>();
}

std::vector<const Tensor *> DivArg0GradOpPattern::touches(Op *) const {
  return {};
}

// grad_out = grad_in / fwd_in1
bool DivArg0GradOpPattern::apply(Op *op) const {
  auto grad_in  = op->inTensor(DivArg0GradOp::getGradInIndex());
  auto fwd_in1  = op->inTensor(DivArg0GradOp::getFwdArg1InIndex());
  auto grad_out = op->outTensor(DivArg0GradOp::getOutIndex());

  // we assume this dynamic_cast call has been confirmed
  // to be valid via a previous call to DivArg0GradOpPattern::matches
  auto axes = dynamic_cast<DivArg0GradOp *>(op)->getReductionAxes();

  // create the new ops
  auto div    = makeReplacementOpInIr(Onnx::AiOnnx::OpSet9::Div, op);
  auto reduce = dynamic_cast<ReduceSumOp *>(
      makeReplacementOpInIr(Onnx::AiOnnx::OpSet9::ReduceSum, op));
  reduce->setAxes(axes);
  // do not keep reduced dims
  reduce->setKeepDims(0l);

  // Remove the DivArg0GradOp
  op->disconnectAllInputs();
  op->disconnectAllOutputs();

  // Connect up the new ops
  div->connectInTensor(0, grad_in->id);
  div->connectInTensor(1, fwd_in1->id);
  div->createAndConnectOutTensor(
      0, grad_in->getIr().createIntermediateTensorId(grad_in->id));
  div->outInfo(0) = op->prettyNpOut(grad_in->info, fwd_in1->info);

  reduce->connectInTensor(0, div->outTensor(0)->id);
  reduce->connectOutTensor(0, grad_out->id);

  // Don't delete op until after the op->prettyNpOut calls.
  op->getGraph().eraseOp(op->id);

  return true;
}

namespace {
static PatternCreator<DivArg0GradOpPattern>
    DivArg0GradOpPattern(PreAliasPatternType::DivArg0GradOp,
                         "DivArg0GradOp",
                         /* enabled = */ true,
                         /* mandatory = */ true);
}

} // namespace popart
