#include <memory>
#include <numeric>
#include <popart/graph.hpp>
#include <popart/op/matmul.hpp>
#include <popart/op/reducesum.hpp>
#include <popart/op/reshape.hpp>
#include <popart/op/squeeze.hpp>
#include <popart/op/transpose.hpp>
#include <popart/patterns/pattern.hpp>
#include <popart/patterns/patterns.hpp>
#include <popart/tensor.hpp>
#include <popart/tensors.hpp>
#include <popart/topocons.hpp>
#include <popart/util.hpp>

namespace popart {
/*
  The intention of this pattern is to make sure that all matmuls have 3D inputs
  of the form  [g x n x m ] i.e. groups x row x colum

                                [a,b]     [b,c]
                                  |         |
                               RESHAPE   RESHAPE
    [a,b] [b,c]                   |         |
      |     |                  [1,a,b]   [1,b,c]
      |     |                       |     |
      MAT MUL      ------>          MAT MUL
         |                             |
         |                          [1,a,c]
       [a,c]                           |
                                    RESHAPE
                                       |
                                     [a,c]
 */

class MatMulPattern : public PreAliasPattern {
public:
  bool matches(Op *op) const override {
    if (op->opid == Onnx::Operators::MatMul_1 ||
        op->opid == Onnx::Operators::MatMul_9) {
      // If the inputs are less than 3d
      auto lhs = op->inTensor(MatMulOp::getLhsInIndex());
      auto rhs = op->inTensor(MatMulOp::getRhsInIndex());

      // Match if either inputs is not a minium 3d tensor
      if (lhs->info.rank() >= 3 && rhs->info.rank() >= 3) {
        return false;
      } else {
        return true;
      }
    } else {
      return false;
    }
  }

  std::vector<const Tensor *> touches(Op *) const override { return {}; }

  void configureReshapeOp(ReshapeOp *op,
                          const Shape &outShape,
                          const TensorId inputTensorId,
                          const TensorId outputTensorId,
                          const double priority = 0) const {
    op->setOutShape(outShape);
    op->priority = priority;
    op->connectInTensor(ReshapeOp::getInIndex(), inputTensorId);
    op->connectOutTensor(ReshapeOp::getOutIndex(), outputTensorId);
    op->setup();
  }

  void configureReshapeOp(ReshapeOp *op,
                          const Shape &outShape,
                          const TensorId inputTensorId,
                          const double priority = 0) const {
    op->setOutShape(outShape);
    op->priority = priority;
    op->connectInTensor(ReshapeOp::getInIndex(), inputTensorId);
    op->createAndConnectOutTensor(ReshapeOp::getOutIndex(),
                                  createIntermediateTensorId(inputTensorId));
    op->setup();
  }

  void configureMatMulOp(MatMulOp *op,
                         const TensorId lhsTensorId,
                         const TensorId rhsTensorId,
                         const TensorId outTensorId) const {
    op->connectInTensor(MatMulOp::getLhsInIndex(), lhsTensorId);
    op->connectInTensor(MatMulOp::getRhsInIndex(), rhsTensorId);
    op->createAndConnectOutTensor(MatMulOp::getOutIndex(), outTensorId);
    op->setup();
  }

  bool apply(Op *op) const override {

    MatMulOp *matmulOp = dynamic_cast<MatMulOp *>(op);

    logging::pattern::debug(
        "Applying MatMulOp pattern to reshape input from {} x{} to {} x {}",
        matmulOp->lhsIn()->info.shape(),
        matmulOp->rhsIn()->info.shape(),
        matmulOp->getExpandedLhsShape(),
        matmulOp->getExpandedRhsShape());

    // The inputs/output tensors of the original matmul
    auto lhs = matmulOp->lhsIn();
    auto rhs = matmulOp->rhsIn();
    auto out = matmulOp->out();

    auto lhsReshapeOp = dynamic_cast<ReshapeOp *>(
        makeReplacementOpInIr(Onnx::Operators::Reshape_5, op, "LhsReshape"));
    auto rhsReshapeOp = dynamic_cast<ReshapeOp *>(
        makeReplacementOpInIr(Onnx::Operators::Reshape_5, op, "RhsReshape"));
    auto outReshapeOp = dynamic_cast<ReshapeOp *>(
        makeReplacementOpInIr(Onnx::Operators::Reshape_5, op, "OutReshape"));

    // expand the lhs input by reshaping it, set the priority to the lowest
    // so that it is scheduled as late as possiable.
    configureReshapeOp(lhsReshapeOp,
                       matmulOp->getExpandedLhsShape(),
                       lhs->id,
                       std::numeric_limits<double>::lowest());

    // expand the rhs input by reshaping it
    configureReshapeOp(rhsReshapeOp,
                       matmulOp->getExpandedRhsShape(),
                       rhs->id,
                       std::numeric_limits<double>::lowest());

    // disconnect the mat mul from it's original inputs & output
    matmulOp->disconnectAllInputs();
    matmulOp->disconnectAllOutputs();

    // Setup the new matmul for 3d inputs
    configureMatMulOp(matmulOp,
                      lhsReshapeOp->outTensor(ReshapeOp::getOutIndex())->id,
                      rhsReshapeOp->outTensor(ReshapeOp::getOutIndex())->id,
                      createIntermediateTensorId(out->id));

    // Reshape the output back the the user defined shape
    configureReshapeOp(outReshapeOp,
                       out->info.shape(),
                       matmulOp->outTensor(MatMulOp::getOutIndex())->id,
                       out->id);

    return true;
  }
};

// The following pattern will expand matmul(lhs/rhs)grad to a transpose and and
// a matmul Additionally it may need to add a squeeze/reduce/reshape to the
// output of the matmul to match the output of the grad op.

class MatMulGradPattern : public PreAliasPattern {
public:
  std::vector<const Tensor *> touches(Op *) const override { return {}; }

  bool apply(Op *) const override;

  std::vector<int64_t> getTransposeDimensions(popart::Tensor *t) const {
    // Transpose the final two dimensions
    auto rank = t->info.rank();

    if (rank < 2) {
      throw error("Rank of input {} it too small for "
                  "MatMulGradPattern::getTransposeDimensions",
                  rank);
    }

    std::vector<int64_t> dims(rank);
    std::iota(dims.begin(), dims.end(), 0);
    std::swap(dims[rank - 2], dims[rank - 1]);
    return dims;
  }

  popart::Tensor *configureReshapeOp(ReshapeOp *op,
                                     const Shape &outShape,
                                     const TensorId inputTensorId,
                                     const TensorId outputTensorId,
                                     const double priority = 0) const {
    op->setOutShape(outShape);
    op->priority = priority;
    op->connectInTensor(ReshapeOp::getInIndex(), inputTensorId);
    op->connectOutTensor(ReshapeOp::getOutIndex(), outputTensorId);
    op->setup();
    return op->outTensor(ReshapeOp::getOutIndex());
  }

  popart::Tensor *configureReshapeOp(ReshapeOp *op,
                                     const Shape &outShape,
                                     const TensorId inputTensorId,
                                     const double priority = 0) const {
    op->setOutShape(outShape);
    op->priority = priority;
    op->connectInTensor(ReshapeOp::getInIndex(), inputTensorId);
    op->createAndConnectOutTensor(ReshapeOp::getOutIndex(),
                                  createIntermediateTensorId(inputTensorId));
    op->setup();
    return op->outTensor(ReshapeOp::getOutIndex());
  }

  popart::Tensor *configureTranposeOp(TransposeOp *op,
                                      const TensorId inputTensorId,
                                      const Shape &perm,
                                      const double priority = 0) const {
    op->setPerm(perm);
    op->priority = priority;
    op->connectInTensor(TransposeOp::getInIndex(), inputTensorId);
    op->createAndConnectOutTensor(TransposeOp::getOutIndex(),
                                  createIntermediateTensorId(inputTensorId));
    op->setup();
    return op->outTensor(TransposeOp::getOutIndex());
  }

  popart::Tensor *configureMatMulOp(MatMulOp *op,
                                    const TensorId lhsTensorId,
                                    const TensorId rhsTensorId,
                                    const TensorId outTensorId) const {
    op->connectInTensor(MatMulOp::getLhsInIndex(), lhsTensorId);
    op->connectInTensor(MatMulOp::getRhsInIndex(), rhsTensorId);
    op->createAndConnectOutTensor(MatMulOp::getOutIndex(), outTensorId);
    op->setup();
    return op->outTensor(MatMulOp::getOutIndex());
  }

  popart::Tensor *configureReduceSumOp(ReduceSumOp *op,
                                       const TensorId inputTensorId,
                                       const Shape &axes,
                                       bool keepDims) const {
    op->setAxes(axes);
    op->setKeepDims(keepDims);
    op->connectInTensor(ReduceSumOp::getInIndex(), inputTensorId);
    op->createAndConnectOutTensor(ReduceSumOp::getOutIndex(),
                                  createIntermediateTensorId(inputTensorId));
    op->setup();
    return op->outTensor(ReduceSumOp::getOutIndex());
  }

  popart::Tensor *configureSqueezeOp(SqueezeOp *op,
                                     const TensorId inputTensorId,
                                     const Shape &axes) const {
    op->setAxes(axes);
    op->connectInTensor(SqueezeOp::getInIndex(), inputTensorId);
    op->createAndConnectOutTensor(SqueezeOp::getOutIndex(),
                                  createIntermediateTensorId(inputTensorId));
    op->setup();
    return op->outTensor(SqueezeOp::getOutIndex());
  }

  virtual popart::Tensor *getIn(Op *op) const      = 0;
  virtual popart::Tensor *getGradIn(Op *op) const  = 0;
  virtual popart::Tensor *getGradOut(Op *op) const = 0;

  virtual InIndex getInIndex() const     = 0;
  virtual InIndex getGradInIndex() const = 0;

  Shape getLhsShape(Op *op) const {
    MatMulBaseOp *matmulOp = dynamic_cast<MatMulBaseOp *>(op);
    return matmulOp->getExpandedLhsShape();
  }
  Shape getRhsShape(Op *op) const {
    MatMulBaseOp *matmulOp = dynamic_cast<MatMulBaseOp *>(op);
    return matmulOp->getExpandedRhsShape();
  }
};

class MatMulLhsGradPattern : public MatMulGradPattern {
public:
  bool matches(Op *op) const override {
    return (op->opid == Onnx::GradOperators::MatMulLhsGrad);
  }

  virtual popart::Tensor *getIn(Op *op) const override {
    return op->inTensor(MatMulLhsGradOp::getRhsInIndex());
  }
  virtual popart::Tensor *getGradIn(Op *op) const override {
    return op->inTensor(MatMulLhsGradOp::getGradInIndex());
  }
  virtual popart::Tensor *getGradOut(Op *op) const override {
    return op->outTensor(MatMulLhsGradOp::getOutIndex());
  }

  virtual InIndex getInIndex() const override {
    return MatMulOp::getRhsInIndex();
  }
  virtual InIndex getGradInIndex() const override {
    return MatMulOp::getLhsInIndex();
  }
};

class MatMulRhsGradPattern : public MatMulGradPattern {
public:
  bool matches(Op *op) const override {
    return (op->opid == Onnx::GradOperators::MatMulRhsGrad);
  }

  virtual popart::Tensor *getIn(Op *op) const override {
    return op->inTensor(MatMulRhsGradOp::getLhsInIndex());
  }
  virtual popart::Tensor *getGradIn(Op *op) const override {
    return op->inTensor(MatMulRhsGradOp::getGradInIndex());
  }
  virtual popart::Tensor *getGradOut(Op *op) const override {
    return op->outTensor(MatMulRhsGradOp::getOutIndex());
  }

  virtual InIndex getInIndex() const override {
    return MatMulOp::getLhsInIndex();
  }
  virtual InIndex getGradInIndex() const override {
    return MatMulOp::getRhsInIndex();
  }
};

bool MatMulGradPattern::apply(Op *op) const {

  auto in       = getIn(op);
  auto grad_in  = getGradIn(op);
  auto simon    = grad_in;
  auto grad_out = getGradOut(op);

  auto lhsShape = getLhsShape(op);
  auto rhsShape = getRhsShape(op);

  logging::pattern::info(
      "Applying {} pattern to replace marmulXXXgradop with matmulop",
      getPatternName());

  auto reshapeOpInExpand = dynamic_cast<ReshapeOp *>(
      makeReplacementOpInIr(Onnx::Operators::Reshape_5, op, "ReshapeIn"));
  auto reshapeOpGradInExpand = dynamic_cast<ReshapeOp *>(
      makeReplacementOpInIr(Onnx::Operators::Reshape_5, op, "ReshapeGradIn"));
  auto transposeOp = dynamic_cast<TransposeOp *>(
      makeReplacementOpInIr(Onnx::Operators::Transpose_1, op, "TransposeIn"));
  auto matmulOp = dynamic_cast<MatMulOp *>(
      makeReplacementOpInIr(Onnx::Operators::MatMul_9, op));
  auto squeezeOp = dynamic_cast<SqueezeOp *>(
      makeReplacementOpInIr(Onnx::Operators::Squeeze_1, op, "Squeeze"));
  auto reduceSumOp = dynamic_cast<ReduceSumOp *>(
      makeReplacementOpInIr(Onnx::Operators::ReduceSum_1, op, "ReduceOut"));
  auto reshapeOp = dynamic_cast<ReshapeOp *>(
      makeReplacementOpInIr(Onnx::Operators::Reshape_5, op, "ReduceOur"));

  // Remove the MatMulXXXGradOp
  op->disconnectAllInputs();
  op->disconnectAllOutputs();
  // Make sure they any constrain on the matmulXXXgradop is moved to the matmul
  // op
  op->getGraph().topoCons->transfer(op, matmulOp);
  op->getGraph().eraseOp(op->id);

  // We will add the reshapes, if they have no effect they will be eliminated
  // from the ir Expand the grad_in tensor to a minimum 3d tensor
  {
    Shape grad_inShape;

    if (getGradInIndex() == MatMulOp::getRhsInIndex()) {
      grad_inShape = rhsShape;
    } else {
      grad_inShape = lhsShape;
    }

    // expand the input by reshaping it
    grad_in = configureReshapeOp(reshapeOpGradInExpand,
                                 grad_inShape,
                                 grad_in->id,
                                 std::numeric_limits<double>::lowest());
  }

  // Expand the in tensor to a minimum 3d tensor
  {
    Shape inShape;

    if (getInIndex() == MatMulOp::getRhsInIndex()) {
      inShape = rhsShape;
    } else {
      inShape = lhsShape;
    }

    // expand the input by reshaping it
    in = configureReshapeOp(reshapeOpInExpand,
                            inShape,
                            in->id,
                            std::numeric_limits<double>::lowest());

    // Add constraint that we will not reshape the in until the grad_in has been
    // produced
    reshapeOpInExpand->getGraph().topoCons->insert(simon->getProducer(),
                                                   reshapeOpInExpand);
  }

  // Configure the tranpose the in tensor
  in = configureTranposeOp(transposeOp,
                           in->id,
                           getTransposeDimensions(in),
                           std::numeric_limits<double>::lowest());

  // Configure the mat mul op
  matmulOp->setCanCreateInputs(false);
  matmulOp->connectInTensor(getGradInIndex(), grad_in->id);
  matmulOp->connectInTensor(getInIndex(), in->id);
  matmulOp->createAndConnectOutTensor(MatMulOp::getOutIndex(),
                                      createIntermediateTensorId(grad_out->id));
  matmulOp->setup();
  auto out = matmulOp->outTensor(MatMulOp::getOutIndex());

  if (out->info.shape() == grad_out->info.shape()) {
    // The output of matmul is correct, remove the intermediate tensor and
    // instead use the grad_out
    matmulOp->disconnectAllOutputs();
    matmulOp->connectOutTensor(MatMulOp::getOutIndex(), grad_out->id);

  } else {

    logging::pattern::debug("{} need to reduce {} to {}",
                            getPatternName(),
                            out->info.shape(),
                            grad_out->info.shape());
    // The output of the matmul needs to be reduced to match the expected output
    auto matmulOutputShape = out->info.shape();

    // First remove any leading '1' dimension with a squeeze
    std::vector<int64_t> squeezeDims;
    for (auto i = 0; i < out->info.shape().size(); ++i) {
      if (out->info.shape()[i] == 1) {
        squeezeDims.push_back(i);
      } else {
        break;
      }
    }

    if (squeezeDims.size() > 0) {
      matmulOutputShape = squeeze(out->info.shape(), squeezeDims);
    }

    if (matmulOutputShape != grad_out->info.shape()) {

      if (grad_out->info.shape().size() == 1 && matmulOutputShape.size() > 0) {

        for (int i = 0; i < out->info.shape().size(); i++) {
          if (out->info.shape()[out->info.shape().size() - 1 - i] == 1) {
            squeezeDims.push_back(out->info.shape().size() - 1 - i);
          } else {
            break;
          }
        }
      }
    }

    if (squeezeDims.size() > 0) {
      logging::pattern::debug("{} squeezing 1's {} from {}",
                              getPatternName(),
                              squeezeDims,
                              out->info.shape());
      out = configureSqueezeOp(squeezeOp, out->id, squeezeDims);
    }

    if (out->info.shape() == grad_out->info.shape()) {
      // The output of transpose/matmul/squeeze is correct, remove the
      // intermediate tensor and instead use the grad_out
      squeezeOp->disconnectAllOutputs();
      squeezeOp->connectOutTensor(SqueezeOp::getOutIndex(), grad_out->id);
    } else {

      // The shapes are still not the same then use the reduce
      popart::Shape targetShape = grad_out->info.shape();
      popart::Shape outShape    = out->info.shape();

      // prepend 1's to the outShape
      while (outShape.size() < targetShape.size()) {
        outShape.insert(outShape.begin(), 1);
      }

      while (outShape.size() > targetShape.size()) {
        targetShape.insert(targetShape.begin(), 1);
      }

      std::vector<std::int64_t> reduceDims;
      for (int i = 0; i < outShape.size(); ++i) {
        if (outShape[i] != targetShape[i]) {
          reduceDims.push_back(i);
        }
      }

      // Reduce & reshape the output
      out = configureReduceSumOp(reduceSumOp, out->id, reduceDims, false);
      configureReshapeOp(
          reshapeOp, grad_out->info.shape(), out->id, grad_out->id);
    }
  }

  // Remove any ops not used
  auto removedIfNotUsed = [](Op *opToRemove) {
    if (opToRemove->inTensorCount() == 0) {
      opToRemove->getGraph().eraseOp(opToRemove->id);
    }
  };

  removedIfNotUsed(reshapeOpInExpand);
  removedIfNotUsed(reshapeOpGradInExpand);
  removedIfNotUsed(squeezeOp);
  removedIfNotUsed(reduceSumOp);
  removedIfNotUsed(reshapeOp);

  return true;
}

// Disabled by default
namespace {
static PatternCreator<MatMulPattern>
    matMulPattern(PreAliasPatternType::MATMULOP, "MatMulOp", true);
static PatternCreator<MatMulLhsGradPattern>
    matMulLhsGradPattern(PreAliasPatternType::MATMULLHSGRADOP,
                         "MatMulLhsGradOp",
                         true);
static PatternCreator<MatMulRhsGradPattern>
    matMulRhsGradPattern(PreAliasPatternType::MATMULRHSGRADOP,
                         "MatMulRhsGradOp",
                         true);
} // namespace

} // namespace popart
