// Copyright (c) 2021 Graphcore Ltd. All rights reserved.
#ifndef POPART_WILLOW_INCLUDE_POPART_POPX_POPOPX_HPP_
#define POPART_WILLOW_INCLUDE_POPART_POPX_POPOPX_HPP_

#include "popart/popx/debugcontextx.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ostream>
#include <set>
#include <snap/Tensor.hpp>
#include <string>
#include <typeinfo>
#include <vector>
#include <poplar/Type.hpp>
#include <popart/error.hpp>
#include <popart/names.hpp>
#include <popart/op.hpp>
#include <popart/popx/preparedtensor.hpp>

#include "popart/operatoridentifier.hpp"
#include <popart/popx/opx.hpp>

namespace snap {
class Graph;

namespace program {
class Sequence;
} // namespace program
} // namespace snap

namespace popart {

class TensorInfo;
class DebugInfo;
class Tensor;

namespace popx {

class ICreatorCandidate;

using ICreatorCandidatePtr = std::shared_ptr<ICreatorCandidate>;
struct UnwindEndpoint;

using UnwindEndpointPtr = std::shared_ptr<UnwindEndpoint>;

class Devicex;
class ViewChangers;

class [[deprecated("Please use popart::popx::Opx instead.")]] PopOpx
    : public Opx {

public:
  // need to pass Devicex down to here, easy
  // access to poplar objects
  PopOpx(Op *, Devicex *);
  ~PopOpx() override;

  // create the input snap::Tensor for input at index with name
  // default : throw error (not all PopOpxs can createInput)
  snap::Tensor createInputTensor(
      InIndex index, const poplar::DebugNameAndId &dnai) const override;
  // default return DEADEND, i.e. unable to create input tensor, and
  // cannot use downstream opxs as candidates to create input
  // tensor
  InputCreatorType getInputCreatorType(InIndex index) const override;

  bool canUnwind(InIndex, OutIndex) const override;

  // Reverses the layout change to an input tensor for an op that returned
  // CANUNWIND
  snap::Tensor unwindTensorLayout(snap::Tensor tensor, InIndex, OutIndex)
      const override;
  view::RegMap unwindRegion(InIndex, OutIndex) const override;

  // If the created or unwound tensor does not conform with the IR specs,
  // an PopOpx may supply a view transformation that transforms that tensor into
  // IR specs
  bool hasCreatorViewChangers(InIndex index) const override;
  ViewChangers getCreatorViewChangers(InIndex index) const override;

  // For some ops (e.g. InitOpx, SubgraphOpx, IoTileCopyOpx)
  // the output tensor is created externally, and must
  // therefore exist before the PopOpx is grown.
  // Lets an PopOpx implementation specify which outputs need an external
  // creator
  bool outputCreatedExternally(OutIndex index) const override;

  // To create a snap::Tensor for input index index0, which
  // snap::Tensors must already exist?
  std::set<TensorId> mustExistBeforeCreate(int index0) const override;

  // To create a snap::Tensor for input index index0, which
  // snap::Tensors must already exist?
  // Allows disjunctive normal form of must exist tensors, i.e.
  // at least one full set of TensorIds in the vector must exist
  DnfTensorIds mustExistBeforeCreateDNF(int index0) const override;

  // adds snap::Tensors to devicex_->popTensors,
  // one for each output of op_.
  void grow(snap::program::Sequence &) const override;
  // Akin to the grow function above except it allows for growing over multiple
  // fragments. This is mostly for CallOp optimisations, the default behaviour
  // is to call the single sequence grow function.
  void grow(std::vector<snap::program::Sequence> &) const override;

  // Get the part id of the Opx grow function that creates the output tensor
  std::set<OpxGrowPartId> getInGrowPartIds(Tensor * inTensor) const override;
  OpxGrowPartId getOutGrowPartId(Tensor * outTensor) const override;

  // Grows only a part of the Opx and caches the generated sequences
  // to be assembled in Opx::grow
  void growPart(OpxGrowPartId id) const override;

  // clone the snap::Tensor identified by its TensorId, and copy the contents
  // of it.
  snap::Tensor cloneNcopy(snap::program::Sequence &, TensorId) const;
  // clone the snap::Tensor and copy the contents of it.
  snap::Tensor cloneNcopy(snap::program::Sequence &,
                          const snap::Tensor &,
                          const std::string name = "") const;

  // Returns the Devicex to which this PopOpx belongs
  const Devicex *getDevicex() const;

  // dv_p->getVirtualGraphId(). Defaults to 0 if virtualGraph is not enabled
  int64_t getVirtualGraphId() const;
  // Returns the virtual graph if enabled, else returns the dv_p->graph
  virtual snap::Graph &graph() const;
  // Returns the top level graph (dv_p->graph)
  snap::Graph &topLevelGraph() const;
  // The default assumes all PopOpx input and output tensors are laid out on the
  // same virtual graph. These methods should be overridden when this is not
  // the case, such as for IpuCopyOpx.
  // Returns the virtual graph for the tensor at InIndex, defaults to graph()
  virtual snap::Graph &srcVirtualGraph(InIndex) const;
  // Returns the virtual graph for the tensor at OutIndex, defaults to graph()
  virtual snap::Graph &dstVirtualGraph(OutIndex) const;
  // shortcut for dv_p->tensors.get
  const snap::Tensor &get(TensorId) const;
  // shortcut for dv_p->tensors.getView
  const snap::Tensor &getView(TensorId) const;
  // shortcut for dv_p->tensors.insert
  void insert(TensorId, const snap::Tensor &) const;

  // shortcut for op_p->input.id(int)
  Tensor *inTensor(InIndex) const;
  // shortcut for op_p->output.id(int)
  Tensor *outTensor(OutIndex) const;

  // the debug info to pass to poplar calls
  const popart::DebugInfo &getDebugInfo() const;

  const poplar::DebugNameAndId getDebugNameAndId(
      const std::string name     = "",
      poplar::SourceLocation loc = poplar::SourceLocation::Current()) const;

  // the debug context for this opx with optional debug postfix name
  poplar::DebugContext debugContext(
      const std::string name     = "",
      poplar::SourceLocation loc = poplar::SourceLocation::Current()) const;

  // shortcut for op_p->input.tensor(int)->info
  const TensorInfo &inInfo(InIndex) const;
  // shortcut for op_p->input.tensor(int)->info.shape()
  const Shape &inShape(InIndex) const;
  // shortcut for op_p->input.tensor(int)->info.shape_szt()
  const std::vector<size_t> inShapeSzt(InIndex) const;
  // shortcut for op_p->input.tensor(int)->info
  const TensorInfo &outInfo(OutIndex) const;
  // shortcut for op_p->output.tensor(int)->info.shape()
  const Shape &outShape(OutIndex) const;
  // shortcut for op_p->output.tensor(int)->info.shape_zt()
  const std::vector<size_t> outShapeSzt(OutIndex) const;

  /**
   * Return the virtual graph associated with input at index in
   * \param in the input index
   * \return the corresponding snap virtual graph
   */
  virtual snap::Graph &inGraph(InIndex in) const;
  /**
   * Return the virtual graph associated with output at index out
   * \param out the output index
   * \return the corresponding snap virtual graph
   */
  virtual snap::Graph &outGraph(OutIndex out) const;

  // Generic function to cast the op to it derived type
  template <class OP> OP &getOp() const {
    OP *d_op = dynamic_cast<OP *>(op_p);
    if (d_op == nullptr) {
      throw error("Failed to cast to op ({}) derived op ({}), type:{} ",
                  typeid(op_p).name(),
                  typeid(d_op).name(),
                  op_p->opid);
    }
    return *d_op;
  }

  // TODO: Reconsider the names of these two verifyOp functions

  // Generic function to test that op is of a given type
  template <class OP> void verifyOp(Op * op, const OperatorIdentifier &opid) {
    // compare domain, type (Relu, etc.), but not version as an op can support
    // multiple versions
    // TODO : Consider passing in a list of support opid's (for each version)
    if (op->opid.domain != opid.domain || op->opid.type != opid.type) {
      throw error("Cannot create opx for {} from {}", opid, op->opid);
    }
  }

  template <class OP>
  void verifyOp(Op * op, std::vector<OperatorIdentifier> opids) {

    for (auto &opid : opids) {
      if (op->opid == opid) {
        return;
      }
    }

    std::ostringstream oss;
    oss << "In PopOpx::verifyOp, for op " << op->str()
        << ". Failed to verify, as valid opids are : ( ";
    for (auto valid : opids) {
      oss << valid << ", ";
    }
    oss << ").";
    throw error(oss.str());
  }

  template <class OP> void verifyOp(Op * op) {
    if (!op->isConvertibleTo<OP>()) {
      throw error("Cannot create opx type from {}", op->opid);
    }
  }

  bool hasInput(InIndex) const;
  bool hasOutput(OutIndex) const;

  // Return underlying Poplar input tensor
  const snap::Tensor &getInTensor(InIndex index) const;

  // Return underlying Poplar output tensor
  const snap::Tensor &getOutTensor(OutIndex index) const;

  // Return input tensor with shape matching IR specifications
  // (aliases getInTensor, but has any respective ViewChangers applied)
  const snap::Tensor &getInView(InIndex index) const;

  // Return output tensor with shape matching IR specifications
  // (aliases getOutTensor, but has any respective ViewChangers applied)
  const snap::Tensor &getOutView(OutIndex index) const;

  bool hasInViewChangers(InIndex index) const;
  const ViewChangers &getInViewChangers(InIndex index) const;
  void setOutViewChangers(OutIndex index, const ViewChangers &changers) const;

  void setOutTensor(OutIndex index, const snap::Tensor &tensor) const;

  TensorId inId(InIndex index) const;
  TensorId outId(OutIndex index) const;

  // shortcut for dv_p->getConst
  snap::Tensor getConst(const poplar::Type &type,
                        const std::vector<size_t> &shape,
                        double val,
                        const std::string &name) const;

  snap::Tensor getScalarVariable(const poplar::Type &type,
                                 const std::string &name) const;

  // Create a tensor of 0s of specified shape
  // The tensor is broadcasted from a scalar value to reduce memory footprint
  snap::Tensor getZerosTensor(
      std::vector<std::size_t>, poplar::Type, std::string) const;

  // The PopOpx outputs that come from any subgraph and need to be prepared
  // This allows growing the data flows through subgraphs independently, and
  // growing the PopOpx that calls the subgraph can be deferred until after all
  // data flows through the called subgraph are grown.
  PreparedTensorInfos getOutputsToPrepare() const override;

  // The PopOpx inputs that go to any subgraph and need to be prepared
  PreparedTensorInfos getInputsToPrepare() const override;

private:
  std::string idStr() const;
};

} // namespace popx
} // namespace popart

#endif // POPART_WILLOW_INCLUDE_POPART_POPX_POPOPX_HPP_
