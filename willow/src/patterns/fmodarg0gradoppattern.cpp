// Copyright (c) 2020 Graphcore Ltd. All rights reserved.
#include <memory>
#include <popart/graph.hpp>
#include <popart/op/cast.hpp>
#include <popart/op/fmod.hpp>
#include <popart/patterns/fmodarg0gradoppattern.hpp>
#include <popart/tensor.hpp>
#include <popart/tensorinfo.hpp>

namespace popart {

static auto createConstTensor(Graph &graph, Ir &ir, const Shape &shape) {
  TensorInfo gradInfo(DataType::INT32, shape);
  std::vector<int32_t> gradData(gradInfo.nelms(), 1);
  const auto &gradId = ir.createIntermediateTensorId("modGradOnes");
  graph.getTensors().addConstInit(gradId, gradInfo, gradData.data());
  return gradId;
}

bool FmodArg0GradOpPattern::matches(Op *op) const {
  return op->isConvertibleTo<FmodArg0GradOp>();
}

// Mimic 'ConstantOfShape':
//   grad_out = constantofshape(arg0.shape, value=1.)
TensorId
FmodArg0GradOpPattern::makeAllReplacementOps(Op *op,
                                             Ir *ir,
                                             const Tensor &gradIn,
                                             const Tensor &fwdIn0,
                                             const Tensor &fwdIn1,
                                             const Tensor &fwdOut) const {
  // Create a constant tensor with type int32 initially. This will be casted to
  // the correct type later.
  auto &graph            = op->getGraph();
  const auto &gradId     = createConstTensor(graph, *ir, fwdIn0.info.shape());
  const auto &gradTensor = graph.getTensors().get(gradId);
  auto castTo            = fwdIn0.info.dataType();
  Op::Settings settings  = op->settings;
  settings.name          = gradId + "_gradCast";
  Op *gradCastOp =
      graph.createOp<CastOp>(Onnx::Operators::Cast_9, castTo, settings);
  transferBaseProperties(op, gradCastOp);
  gradCastOp->connectInTensor(CastOp::getInIndex(), gradTensor->id);
  auto castOutId = graph.getIr().createIntermediateTensorId(gradId);
  gradCastOp->createAndConnectOutTensor(CastOp::getOutIndex(), castOutId);
  gradCastOp->setup();
  return castOutId;
}

namespace {
static PatternCreator<FmodArg0GradOpPattern>
    FmodArg0GradOpPattern(PreAliasPatternType::FmodArg0GradOp,
                          "FmodArg0GradOp",
                          /* enabled = */ true,
                          /* mandatory = */ true);
}

} // namespace popart
