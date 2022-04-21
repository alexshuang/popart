// Copyright (c) 2018 Graphcore Ltd. All rights reserved.
#ifndef GUARD_NEURALNET_FUSER_HPP
#define GUARD_NEURALNET_FUSER_HPP

#include <popart/patterns/pattern.hpp>

namespace popart {

// {(a),    (b), (c)} ->     [op0] ->          (out0)
// {(out0), (e), (f)} ->     [op1] ->          {(g), (h)}
//                     ==================>
// {(a), (b), (c)} ->        [op01]         -> {(g), (h)}
// where op1 is the only consumer of out0.
class Fuser : public PreAliasPattern {
public:
  bool matches(Op *) const final;
  // Only (d) is touched. Therefore, a Pattern where [op1] and
  // [op01] perform inplace changes to an input tensor should
  // not inherit from Fuser.
  std::vector<const Tensor *> touches(Op *) const final;
  bool apply(Op *) const final;

private:
  // OpType of op0 in schematic
  virtual const OperatorIdentifier &get0() const = 0;
  // OpType of op1 in schematic
  virtual const OperatorIdentifier &get1() const = 0;
  // how to create a new op01 and move it into Ir
  virtual OpId moveMergedIntoIr(Op *baseOp) const = 0;
};

} // namespace popart

#endif
