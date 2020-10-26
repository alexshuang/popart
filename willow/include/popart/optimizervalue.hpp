// Copyright (c) 2019 Graphcore Ltd. All rights reserved.
#ifndef GUARD_NEURALNET_OPTIMIZERVALUE_HPP
#define GUARD_NEURALNET_OPTIMIZERVALUE_HPP

#include <tuple>

namespace popart {

/**
 * A class used to represent values of hyper parameters.
 */
class OptimizerValue {
public:
  /// Equivalent to OptimizerValue(0, false).
  OptimizerValue() = default;
  /// Equivalent to OptimizerValue(v, true).
  OptimizerValue(float v) : val_(v), isConst_(true) {}
  /// Constructor.
  /// \param v the current value of the hyper parameter.
  /// \param c a boolean flag to indicate whether the parameter will remain
  ///     at this value forever (`true`) or may change over time (`false`).
  OptimizerValue(float v, bool c) : val_(v), isConst_(c) {}
  OptimizerValue(std::pair<float, bool> x)
      : OptimizerValue(x.first, x.second) {}

  OptimizerValue(const OptimizerValue &) = default;
  ~OptimizerValue()                      = default;
  OptimizerValue &operator               =(const OptimizerValue &rhs);

  // current value
  float val() const { return val_; }

  // can the user not change this value in the final computation Graph
  bool isConst() const { return isConst_; }

  bool validReplacement(const OptimizerValue &rhs) const;

private:
  float val_;
  bool isConst_;
};

} // namespace popart

#endif
