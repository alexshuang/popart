#include <popart/graph.hpp>
#include <popart/op/nll.hpp>
#include <popart/op/softmax.hpp>
#include <popart/patterns/patterns.hpp>
#include <popart/patterns/softmaxgraddirect.hpp>
#include <popart/tensorindex.hpp>

namespace popart {

// NLLGRAD (0) -> x -> SOFTMAXGRAD.
const OperatorIdentifier &SoftmaxGradDirect::get0() const {
  return Onnx::CustomGradOperators::NllGrad;
}

// NLLGRAD -> x -> SOFTMAXGRAD (1).
const OperatorIdentifier &SoftmaxGradDirect::get1() const {
  return Onnx::GradOperators::SoftmaxGrad;
}

OpId SoftmaxGradDirect::moveMergedIntoIr(Op *opRoot) const {
  // The root of the pattern is an NLLGrad,
  // we need to move from it to the SoftmaxOp
  Graph &graph = opRoot->getGraph();
  Op *nllgrad  = opRoot;

  return graph.moveIntoGraph(std::unique_ptr<Op>(new SoftmaxGradDirectOp(
      dynamic_cast<NllGradOp *>(nllgrad)->nlll(), nllgrad->getSettings())));
}

namespace {
static PatternCreator<SoftmaxGradDirect>
    PreUniReplPattern(PreAliasPatternType::SOFTMAXGRADDIRECT,
                      "SoftmaxGradDirect");
}

} // namespace popart
