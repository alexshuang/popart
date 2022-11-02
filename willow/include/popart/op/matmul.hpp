// Copyright (c) 2018 Graphcore Ltd. All rights reserved.
#ifndef POPART_WILLOW_INCLUDE_POPART_OP_MATMUL_HPP_
#define POPART_WILLOW_INCLUDE_POPART_OP_MATMUL_HPP_

#include <cstdint>
#include <iosfwd>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <popart/op.hpp>
#include <popart/vendored/optional.hpp>

#include "popart/datatype.hpp"
#include "popart/names.hpp"
#include "popart/tensorinfo.hpp"

namespace popart {
class OpSerialiserBase;
class Tensor;
struct OperatorIdentifier;

enum class MatMulPartialsType { HALF, FLOAT };

std::string toString(const MatMulPartialsType &);
std::ostream &operator<<(std::ostream &, const MatMulPartialsType &);

/**
 * The matmul op supports inputs of IR datatype FLOAT8_143 and FLOAT8_152.
 * Inputs of this are a special case because they type require an additional
 * scalar INT32 tensor input known as the `log2Scale`. This argument may
 * only be used if and only if the two matmul operands are one of the
 * FLOAT8_* types.
 *
 * If the matmul inputs are valid FLOAT8 and log2Scale inputs then the
 * matmul is considered a 'pow2 scaled matmul'. A pow2 scaled matmul
 * is an operation of the form `result := A @ B * 2^(log2scale)` where
 * @ is the matrix multiply op. In this case, the output and partials type
 * must be FLOAT16. Note that the multiplication by 2^(log2scale) is handled
 * by Poplar and is not listed as an Op in the IR.
 */
class MatMulBaseOp : public Op {
public:
  // The phase of the matmul. Needed so when grad matmuls are
  // converted to normal matmuls in preperation for outlining,
  // they remember what they where originally so we can use the
  // correct poplar fullyConnectedPass option
  enum class Phase { Fwd, BwdLHS, BwdRHS };

  struct SerialiseSettings {
    enum class Mode { None, InputChannels, ReducingDim, OutputChannels };

    Mode mode           = Mode::None;
    uint32_t factor     = 0;
    bool keep_precision = false;
  };

  MatMulBaseOp(const OperatorIdentifier &_opid,
               const Op::Settings &settings_,
               const Phase phase_,
               const nonstd::optional<float> availableMemoryProportion_,
               const SerialiseSettings &serialization_,
               const OptionalDataType outputType_,
               const MatMulPartialsType partialsType_,
               const bool enableFullyConnectedPass_ = true);
  MatMulBaseOp(const MatMulBaseOp &)         = default;
  ~MatMulBaseOp() override                   = default;
  std::unique_ptr<Op> clone() const override = 0;

  // Return the expanded shape of the lhs input to matmul
  // minium shape G x N x M
  virtual Shape getExpandedLhsShape() const = 0;

  // Return the expended shape of the rhs input to matmul
  // minium shape G x N x M
  virtual Shape getExpandedRhsShape() const = 0;

  bool useFullyConnectedPass() const;
  void setUseFullyConnectedPass(bool b) { enableFullyConnectedPass = b; }

  nonstd::optional<float> getAvailableMemoryProportion() const {
    return availableMemoryProportion;
  }
  void setAvailableMemoryProportion(const nonstd::optional<float> v) {
    availableMemoryProportion = v;
  }

  const SerialiseSettings &getSerialiseSettings() const {
    return serialization;
  }

  SerialiseSettings &getSerialiseSettings() { return serialization; }

  OptionalDataType getOutputType() const { return outputType; }

  Phase getPhase() const { return phase; }
  void setPhase(Phase p) { phase = p; }

  void appendOutlineAttributes(OpSerialiserBase &os) const override;
  void appendMore(OpSerialiserBase &os) const override;

  MatMulPartialsType getPartialsType() const { return partialsType; }
  void setPartialsType(const MatMulPartialsType &pt) { partialsType = pt; }

  bool canShard() const override { return true; }

protected:
  Phase phase;

  bool enableFullyConnectedPass;

  nonstd::optional<float> availableMemoryProportion;

  SerialiseSettings serialization;

  // Using optional as the input info is not known when initialising
  OptionalDataType outputType;

  MatMulPartialsType partialsType;
};

class MatMulOp : public MatMulBaseOp {
public:
  MatMulOp(const OperatorIdentifier &_opid,
           const Op::Settings &settings_,
           const nonstd::optional<float> &availableMemoryProportion,
           const SerialiseSettings &serialization_,
           const OptionalDataType &outputType,
           const MatMulPartialsType &partialsType_ = MatMulPartialsType::FLOAT);
  MatMulOp(const MatMulOp &) = default;
  MatMulOp &operator=(const MatMulOp &) = delete;
  ~MatMulOp() override                  = default;

  std::vector<std::unique_ptr<Op>> getGradOps() final;
  void setup() final;
  std::unique_ptr<Op> clone() const final;

  static InIndex getLhsInIndex() { return 0; }
  static InIndex getRhsInIndex() { return 1; }
  static InIndex getLog2ScaleInIndex() { return 2; }
  static OutIndex getOutIndex() { return 0; }

  const Tensor *lhsIn() const;
  const Tensor *rhsIn() const;
  const Tensor *log2ScaleIn() const;
  const Tensor *out() const;

  // Return the expanded shape of the inputs & output to matmul
  Shape getExpandedLhsShape() const override { return lhsShape; }
  Shape getExpandedRhsShape() const override { return rhsShape; }
  Shape getExpandedOutShape() const { return outShape; }

  // set/get the option for matmul to create it's inputs
  void setCanCreateInputs(bool value) { canCreateInputs = value; }
  bool getCanCreateInputs() const { return canCreateInputs; }

  float getSubgraphValue() const final { return getHighSubgraphValue(); }

  // Follow the numpy matmul broadcasting rules for the output shape
  Shape npMatMulOut(Shape lhs, Shape rhs);

  bool isPow2ScaledMatMul() const;

  std::set<InIndex> optionalInputs() const override {
    return {getLog2ScaleInIndex()};
  }

private:
  // Verifies the input shapes are valid and throws and exception if not
  void verifyInputShapes(const Shape &lhs, const Shape &rhs) const;

  // Flag to indicate if mat mul can create it's inputs.
  // MatMulGradXXOps converted to MatMulOps don't create their inputs
  bool canCreateInputs = true;

  // The expanded shapes of inputs & outputs. They will
  // be a minium of a 3D shapes
  Shape lhsShape;
  Shape rhsShape;
  Shape outShape;
};

class MatMulBaseGradOp : public MatMulBaseOp {
public:
  MatMulBaseGradOp(const OperatorIdentifier &_opid,
                   const MatMulOp &fwdOp,
                   Phase phase);
  MatMulBaseGradOp(const MatMulBaseGradOp &) = default;
  ~MatMulBaseGradOp() override               = default;
  std::unique_ptr<Op> clone() const override = 0;

  const MatMulOp *getCloneOfCreator() const;

  float getSubgraphValue() const override { return getHighSubgraphValue(); }

protected:
  TensorInfo fwdOpOutputGrad;
  TensorInfo fwdOpLhsInfo;
  TensorInfo fwdOpRhsInfo;

  std::shared_ptr<Op> cloneOfCreator;
};

class MatMulLhsGradOp : public MatMulBaseGradOp {
public:
  MatMulLhsGradOp(const MatMulOp &op_);
  MatMulLhsGradOp(const MatMulLhsGradOp &) = default;
  MatMulLhsGradOp &operator=(const MatMulLhsGradOp &) = delete;
  ~MatMulLhsGradOp() override                         = default;

  static InIndex getGradInIndex() { return 0; }
  static InIndex getRhsInIndex() { return 1; }
  static OutIndex getOutIndex() { return 0; }

  void setup() final;
  std::unique_ptr<Op> clone() const override;
  const std::vector<GradInOutMapper> &gradInputInfo() const final;
  const std::map<int, int> &gradOutToNonGradIn() const final;

  // Return the expanded shape of the inputs. Note that the tranpose of the rhs
  // is done inside the matmul
  Shape getExpandedLhsShape() const override {
    return getCloneOfCreator()->getExpandedOutShape();
  }
  Shape getExpandedRhsShape() const override {
    return getCloneOfCreator()->getExpandedRhsShape();
  }

  // The ONNX tensor shape
  // The shape of the grad op's gradient input
  Shape getGradInputShape() const;
  // The shape of the grad op's rhs input
  Shape getRhsInputShape() const;
  // The shape of the grad op's output
  Shape getOutputShape() const;
};

class MatMulRhsGradOp : public MatMulBaseGradOp {
public:
  MatMulRhsGradOp(const MatMulOp &op_);
  MatMulRhsGradOp(const MatMulRhsGradOp &) = default;
  MatMulRhsGradOp &operator=(const MatMulRhsGradOp &) = delete;
  ~MatMulRhsGradOp() override                         = default;

  static InIndex getGradInIndex() { return 0; }
  static InIndex getLhsInIndex() { return 1; }
  static OutIndex getOutIndex() { return 0; }

  void setup() final;
  std::unique_ptr<Op> clone() const override;
  const std::vector<GradInOutMapper> &gradInputInfo() const final;
  const std::map<int, int> &gradOutToNonGradIn() const final;

  // Return the expanded shape of the inputs. Note that the tranpose of the rhs
  // is done inside the matmul
  Shape getExpandedLhsShape() const override {
    return getCloneOfCreator()->getExpandedLhsShape();
  }
  Shape getExpandedRhsShape() const override {
    return getCloneOfCreator()->getExpandedOutShape();
  }

  // The ONNX tensor shape
  // The shape of the grad op's gradient input
  Shape getLhsInputShape() const;
  // The shape of the grad op's rhs input
  Shape getGradInputShape() const;
  // The shape of the grad op's output
  Shape getOutputShape() const;
};

} // namespace popart

#endif // POPART_WILLOW_INCLUDE_POPART_OP_MATMUL_HPP_
