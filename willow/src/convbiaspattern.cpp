#include <poponnx/addbias.hpp>
#include <poponnx/conv.hpp>
#include <poponnx/ir.hpp>
#include <poponnx/tensor.hpp>

#include <poponnx/convbiaspattern.hpp>

#include <iostream>

namespace willow {

bool ConvBiasPattern::matches(Op *op) const {
  return (op->opType == OpType::CONV) && (op->input.n() == 3);
}

std::vector<const Tensor *> ConvBiasPattern::touches(Op *) const { return {}; }

void ConvBiasPattern::apply(Op *op) const {
  const auto conv = dynamic_cast<ConvOp *>(op);

  std::unique_ptr<Op> add_bias_op(new AddBiasOp(conv));
  const auto tmp_tensor_id = "prebias" + conv->output.id(0);

  op->pir->getTensors().addActGrad(tmp_tensor_id);

  const auto b  = conv->input.tensor(ConvOp::biasInIndex());
  const auto t  = op->pir->getTensors().get(tmp_tensor_id);
  const auto a1 = conv->output.tensor(ConvOp::dataInIndex());

  const auto add_bias = add_bias_op.get();

  op->pir->moveIntoIr(std::move(add_bias_op));

  t->info = a1->info;
  t->setProducer(conv);
  t->consumers.increment(add_bias);
  b->consumers.increment(add_bias);
  b->consumers.decrement(conv);
  a1->resetProducer(add_bias);

  conv->input.erase(ConvOp::biasInIndex());
  add_bias->input.insert(AddBiasOp::dataInIndex(), t);
  add_bias->input.insert(AddBiasOp::biasInIndex(), b);

  conv->output.reset(0, t);
  add_bias->output.insert(0, a1);
}

} // namespace willow
