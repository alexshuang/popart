// Copyright (c) 2018 Graphcore Ltd. All rights reserved.
#ifndef GUARD_NEURALNET_OP_HPP
#define GUARD_NEURALNET_OP_HPP

#include <memory>
#include <set>
#include <unordered_set>
#include <vector>
#include <popart/attributes.hpp>
#include <popart/basicoptionals.hpp>
#include <popart/names.hpp>
#include <popart/opidentifier.hpp>
#include <popart/region.hpp>
#include <popart/scope.hpp>
#include <popart/tensorinfo.hpp>
#include <popart/tensorlocation.hpp>
#include <popart/util.hpp>
#include <popart/vertex.hpp>

#include <popart/subgraph/subgraphnames.hpp>

namespace popart {

enum class RecomputeType { Undefined = 0, Checkpoint, Recompute, Recomputed };

enum class ExecutionContext {
  Normal = 0,
  AccumulateOuterFragment,
  WeightsFromHostFragment,
  WeightsToHostFragment,
  Subgraph
};

std::ostream &operator<<(std::ostream &, const RecomputeType &);
std::ostream &operator<<(std::ostream &, const ExecutionContext &);

class OpSerialiserBase;

// the input tensor of a grad-op has what kind of
// relationship with the corresponding non-grad-op?
// design note: it's not possible for an input to a
// grad-op to NOT be directly related to
// the corresponding non-grad-op.
enum class GradOpInType { In = 0, Out, GradOut };

class GradInOutMapper {
public:
  GradInOutMapper(InIndex iGrad_, int iNonGrad_, GradOpInType);
  // input index to a grad-op
  InIndex iGrad;
  // "input/output/gradient-of-output" index to
  // corresponding non-grad op,
  int iNonGrad;
  // where "input/output/gradient-of-output" above is
  GradOpInType type;

  bool operator==(const GradInOutMapper &rhs) const;
};

class Op : public Vertex {
public:
  // We use pointers to TensorIndexMaps for PIMPL reasons.
  // Note that we cannot initialise these with {nullptr} on gcc.
  // They are initialised in the Op constuctors
  // The consumed Tensors
  std::unique_ptr<TensorIndexMap> input;
  // The produced Tensors
  std::unique_ptr<TensorIndexMap> output;

  // The unique identifier of the Op (will always be set in Op::Op)
  OpId id{-1};

  // The operation type, domain & version
  //   A given operator is identified by a three-tuple: (domain, op_type, and
  //   op_version). This is written as domain.op_type:op_version in prose (e.g.,
  //   com.acme.FastConv:3). Nodes in graphs always refer to operators by their
  //   three-part identifier.
  OperatorIdentifier opid;

  bool pruneable = true;

  // To flag an Op as being part of the optimizer
  bool optimizerOp = false;

  struct Settings {

    Settings(Graph &graph_, const std::string &name_)
        : graph(graph_), name(name_) {}
    Settings(Graph &graph_, const std::string &name_, const Scope &scope_)
        : graph(graph_), name(name_), scope(scope_) {}
    virtual ~Settings()        = default;
    Settings(const Settings &) = default;

    std::reference_wrapper<Graph> graph;

    std::string name = "";

    Scope scope;
    RecomputeType recomputeType   = RecomputeType::Undefined;
    TensorLocation tensorLocation = TensorLocation();

    // optional inplace priorities, to take precedence over the default
    // priorities. A negative priority gurarantees no inplacing
    // This should really be a map with "OperatorIdentifier" keys, see T6783
    std::vector<std::tuple<std::string, float>> inplacePriorityVeto;

    // A set of patterns which should not be applied to this op.
    std::unordered_set<std::string> excludePatterns;

    // The virtual graph this op has been assigned to if set
    OptionalVGraphId vgraphId;

    OptionalPipelineStage pipelineStage;

    // The execution phase this op has been assigned to if set
    OptionalExecutionPhase executionPhase;

    OptionalBatchSerializedPhase batchSerializedPhase;

    // If the OP should be placed on I/O tiles instead of regular tiles
    TileSet tileSet{TileSet::Compute};

    // If the OP needs to run in a special fragment,
    // such as gradient accumulation
    ExecutionContext executionContext{ExecutionContext::Normal};

    // Tensor layout mapping should be inferred "to" tensor <- "from" tensor
    std::map<InIndex, InIndex> inferTensorMappingToFrom;

    // all Ops will be topologically sorted "as close to" the order of
    // priority (highest to lowest) while still resulting in a valid
    // topological ordering.
    // default : 0.0
    double schedulePriority{0.0};

    // Extra attributes to differentiate ops for outlining
    // Ops with different outline attributes are not outlined together
    std::map<std::string, std::string> extraOutlineAttributes;

    // This method will append the optional attributes (vgraphId, etc)
    // depending on whether the attribute has been
    // set in the onnx model.
    virtual void setFromAttributes(const Attributes &attributes);

    Ir &getIr() const;
  };

  Settings settings;

  Settings &getSettings() { return settings; }
  const Settings &getSettings() const { return settings; }

  const OptionalVGraphId getOptionalVGraphId() const;
  VGraphId getVirtualGraphId() const;
  virtual VGraphIdAndTileSet getIntrospectionInVirtualGraphId(InIndex) const;
  virtual VGraphIdAndTileSet getIntrospectionOutVirtualGraphId(OutIndex) const;
  void setVirtualGraphId(const OptionalVGraphId);
  bool hasVirtualGraphId() const;

  const OptionalExecutionPhase getOptionalExecutionPhase() const;
  virtual ExecutionPhase getExecutionPhase() const;
  void setExecutionPhase(const OptionalExecutionPhase);
  bool hasExecutionPhase() const;

  const OptionalBatchSerializedPhase getOptionalBatchSerializedPhase() const;
  virtual BatchSerializedPhase getBatchSerializedPhase() const;
  void setBatchSerializedPhase(const OptionalBatchSerializedPhase);
  bool hasBatchSerializedPhase() const;

  bool isExcludedFromPattern(const Pattern *) const;

  void setPipelineStage(OptionalPipelineStage);
  bool hasPipelineStage() const;
  PipelineStage getPipelineStage() const;
  OptionalPipelineStage getOptionalPipelineStage() const;

  virtual int getInBatchAxis(InIndex) const { return 0; }
  virtual int getOutBatchAxis(OutIndex) const { return 0; }

  // Inherit placement attributes:
  // - Pipeline stage
  // - Execution phase
  // - Virtual graph ID
  // - Batch serial phase
  void inheritPlacementAttributes(bool inheritSerializations);

  Ir &getIr();
  const Ir &getIr() const;

  Graph &getGraph() { return settings.graph.get(); }
  const Graph &getGraph() const { return settings.graph.get(); }

  const Scope &getScope() const { return settings.scope; }
  void setScope(const Scope &scope) { settings.scope = scope; }

  const std::string &getName() const { return settings.name; }
  void setName(const std::string &name) { settings.name = name; }

  virtual bool isNorm() const;
  bool isElementWiseUnary() const;

  // Methods used by patterns to determine if an op can be replaced by another
  // op

  // Return true if the op based on it's configuration can be replace by the
  // identity operations, else false.
  virtual bool canBeReplacedByIdentity();

public:
  Op(const OperatorIdentifier &_opid, const Op::Settings &settings);

  // Note: copy constructor does NOT copy input and output
  Op(const Op &);
  Op &operator=(const Op &) = delete;
  // A c++ aside: the rule-of-3 says that it's good
  // practise to have an explicit destructor,
  // given that there is an explict copy con.
  // But not really nec. as Vertex has a virtual
  // destructor.
  virtual ~Op();

  std::string str() const final;
  std::string debugName() const;

  // create an ActGrad (output) tensor
  // and wire it to this Op's output
  void createAndConnectOutTensor(OutIndex, TensorId);

  void append(std::stringstream &ss) const;

  void toJSON(std::stringstream &ss) const;

  // sum of the total memory of all output tensors
  // We might want a cycle counter too for more sophisticated recomputation
  int64_t memOfOutputs() const;

  virtual std::set<InIndex> optionalInputs() const { return {}; }

  // wire a tensor to input: updates input and
  // updates consumers of tensor with id TensorId
  void defaultConnectInTensor(InIndex, TensorId);

  virtual void connectInTensor(InIndex, TensorId);

  void connectOutTensor(OutIndex, TensorId);

  // Disconnect an input test from the op
  void disconnectInTensor(Tensor *tensor);
  virtual void disconnectInTensor(InIndex, Tensor *tensor);

  // Disconnect an output tensor from the op
  void disconnectOutTensor(Tensor *tensor);

  // Disconnect all input tensors
  void disconnectAllInputs();

  // Disconnect all output tensors
  void disconnectAllOutputs();

  const std::string &name() const;

  // set shape and type parameters,
  // This function MUST set output
  // TensorInfos for all outputs
  virtual void setup();

  // return a vector of 1 or several gradient Ops: for
  // obtaining the gradient of the inputs of this Op.
  // If this Op is already a gradient Op, throws error
  // Why is this not constant? For one, nOps counter increments.
  virtual std::vector<std::unique_ptr<Op>> getGradOps();

  // What are the variants of this Op (if any) which can
  // modify / alias the inputs at the given indices?
  // This function doesn't check for anchor violations
  // or topological order violations. When there are several,
  // they should be returned in descending order of preference

  virtual std::vector<std::tuple<OperatorIdentifier, float>>
  inplacePriorityDefault() const;

  virtual std::unique_ptr<Op>
  getInplaceVariant(const OperatorIdentifier &) const;

  // The input Region which this Op modifies (for inplace ops)
  virtual view::Regions modifies(InIndex) const;
  // The input Region which this Op uses
  virtual view::Regions uses(InIndex) const;
  // The input Region which the output will alias (for inplace and view-changing
  // ops)
  virtual view::Regions aliases(InIndex, OutIndex) const;
  // Map used regions of the input to/from the output (we assume the same for
  // modifies, aliases, uses)
  virtual view::RegMap fwdRegMap(InIndex, OutIndex) const;
  virtual view::RegMap bwdRegMap(InIndex, OutIndex) const;

  /** Is modifies(i) non-empty for any input index i?
   *
   * \returns     true if modifies(i) is non-empty for any i
   * \returns     false otherwise.
   */
  bool modifies() const;
  /** Check if an op modifies a tensor at a specific index in.
   *
   * \param in    Index to check
   * \returns     true if it modifies the tensor.
   * \returns     false otherwise.
   */
  bool modifiesIndex(InIndex in) const;
  /** Check if an op overwrites a tensor at a specific index in.
   *
   * \param t      Tensor to check
   * \returns     true if it overwrites the tensor.
   * \returns     false otherwise.
   */
  bool overwritesTensor(Tensor *t) const;

  // A grad-op outputs an edge-gradient tensor dT at gradOpOutIndex.
  // dT is the edge-gradient of a tensor T which was the input
  // to grad-op's non-grad partner. At what index was T the input
  // of non-grad-op? If not relevant (non-grad-ops) throw an error
  virtual int getNonGradInIndex(int gradOpOutIndex) const;

  // For grad-ops, matching input indices to
  // corresponding IN/OUT/GRADOUT indices of
  // corresponding non-grad-op.
  // throws an error if not appropriate (non-grad ops)
  virtual const std::vector<GradInOutMapper> &gradInputInfo() const;

  // return the full map corresponding to getNonGradInIndex.
  // throws an error if not appropriate (non-grad)
  virtual const std::map<int, int> &gradOutToNonGradIn() const;

  // for non-grad-op `op', takes in the set of output indices
  // of `op' for which a gradient is available and returns
  // if all the gradients needed to create grad-ops are present
  // currently this will just compare the size of
  // the set passed in with number of paths to final loss
  bool readyToCreateGradients(std::set<int> &) const;

  // return a copy of self, similar to
  // cpppatterns.com/patterns/virtual-constructor.html
  // some people call it "covariant return type"
  // Throws error from this class if not implemented
  virtual std::unique_ptr<Op> clone() const = 0;

  template <typename T> bool isConvertibleTo() const {
    return dynamic_cast<const T *>(this) != nullptr;
  }

  // Is this Op a LossOp (nll, l1loss, etc)? Note:
  // the Sum op which adds the losses together is not
  // a LossOp
  virtual bool isLossOp() const;

  virtual bool isIpuCopyOp() const;

  // Returns true for Ops that copy only optimizer tensors
  // from one IPU to another
  virtual bool copiesOptimizerTensors() const;

  // Op that is part of the optimizer
  virtual bool isOptimizerOp() const;

  // The random seed tensor used to set the IPU's RNGs is created
  // in the IR, and connected to the Ops that require it
  virtual bool requiresRandomSeed() const;
  virtual InIndex getSeedInIndex() const;

  bool hasInput(InIndex index) const;

  // helper functions, access fields of input and output
  Tensor *inTensor(InIndex index);
  const Tensor *inTensor(InIndex index) const;
  Tensor *outTensor(OutIndex index);
  const Tensor *outTensor(OutIndex index) const;

  TensorId inId(InIndex index);
  const TensorId inId(InIndex index) const;
  TensorId outId(OutIndex index);
  const TensorId outId(OutIndex index) const;

  TensorInfo &inInfo(InIndex index);
  const TensorInfo &inInfo(InIndex index) const;
  TensorInfo &outInfo(OutIndex index);
  const TensorInfo &outInfo(OutIndex index) const;

  const Shape &inShape(InIndex index) const;
  const Shape &outShape(OutIndex index) const;

  size_t inTensorCount() const;
  size_t outTensorCount() const;

  Rank inRank(InIndex index) const;
  Rank outRank(OutIndex index) const;

  OutIndex outIndex(Tensor *) const;

  // Virtual method to append the op attributes to the stream. This method
  // should be overridden if the derived class has additional attributes.
  virtual void appendAttributes(OpSerialiserBase &) const;

  // Virtual method to append the op attributes that are relevant for outlining
  // ops. Ops should override this function if there are additional attributes.
  // Two ops with identical type and outline attributes can be outlined and are
  // supposed to be functionally equivalent.
  virtual void appendOutlineAttributes(OpSerialiserBase &) const;

  // All graph that this op may call during its execution
  virtual std::vector<const Graph *> getCalledGraphs() const;

  // The op inputs that are used as inputs for the graph,
  // in the order they will be used for the graph.
  virtual std::vector<TensorId> getInputsForGraph(const Graph &) const;

  // Calculate numpy broadcast shape for two shapes or generate an error if
  // the broadcast is not aligned. The error will have operator context.
  Shape prettyNpOut(const Shape &s0, const Shape &s1) const;
  TensorInfo prettyNpOut(const TensorInfo &i0, const TensorInfo &i1) const;

protected:
  virtual void appendMore(OpSerialiserBase &) const;

public:
  // The functionality required for sub-graph matching
  using SubgraphInSig =
      std::tuple<Op *, fwtools::subgraph::OutIndex, std::string>;

  std::string getSubgraphEquivId() const;

  std::map<fwtools::subgraph::InIndex, SubgraphInSig> getSubgraphInputs() const;

  // all the consumers at a given output index
  std::map<fwtools::subgraph::OutIndex, std::set<Op *>>
  getSubgraphOutputs() const;

  // default high value here means that sub-graphs
  // of single Ops are cached by default
  virtual float getSubgraphValue() const = 0;

  // for example, conv has this value in getSubgraphValue(),
  constexpr float getHighSubgraphValue() const { return 1000.0f; }
  // and relu has this value.
  constexpr float getLowSubgraphValue() const { return 0.1f; }

  // Allow an op to exclude itself from caching. If this method returns false
  // it will mean that any possiable subgraph that this op is part of will
  // not be cached. The default is enabled (return true)
  virtual bool isOutlineable() const;

  virtual bool hasSideEffect() const;

  bool inputsUnmodifiable() const;

  // A summary of the calculation in inputsUnmodifiable()
  std::string getInputsUnmodifiableString() const;

  bool consumesAnchor() const;
  bool producesAnchor() const;
  bool consumesCheckpointAndIsRecompute() const;
  bool consumesImplicitLoopInput() const;
  bool consumesRestoredInplaceTensor() const;
  bool consumesGraphOutput() const;
  bool producesGraphOutput() const;

  //
  /** Check if input is a variable or aliases a variable.
   *
   * \param in    InIndex to check.
   * \returns     true if any connected variable tensor has a non-empty alias
   *              chain.
   * \returns     false otherwise.
   */
  bool inputVariableOrAlias(InIndex in) const;

  /** Check if output is modified by any consumer.
   *
   * \param out   OutIndex to check.
   * \returns     true if any consumer of any aliased tensor downstream modifies
   *              a non-empty region.
   * \returns     false otherwise.
   */
  bool hasAliasedModifiers(OutIndex out) const;
  // Helper functions for probing graph structure.
  bool isParentOf(const Op *) const;
  bool isChildOf(const Op *) const;

  // Test if the operation can be sharded into multiple operations
  // TODO: T16743: extend support for other dimensions than the batch
  virtual bool canShard() const;

  // Shard operation into multiple operations according to the new,
  // already sharded input tensors. Returns the sharded output tensors.
  // TODO: T16743: extend support for other dimensions than the batch
  virtual std::map<TensorId, std::vector<TensorId>>
  shard(const std::map<TensorId, std::vector<TensorId>> &inputs);

  // Configure attributes/settings on sharded op
  virtual void configureShardedOp(Op *const shardedOp, int shardIndex) const;

  // Return which inputs/outputs are replicated tensor sharding pairs
  virtual ReplicatedTensorShardingIndices
  getReplicatedTensorShardingIndices() const;

  // Configure the operation for replicated tensor sharding at the specific
  // indices
  virtual void
  configureForReplicatedTensorSharding(ReplicatedTensorShardingIndices indices);

protected:
  // Attempt to get the data of an input tensor. This method will throw an
  // exception if it could not access the data.
  void getInTensorData(TensorId tensorId,
                       std::vector<int64_t> &data,
                       std::vector<DataType> dataTypes = {DataType::INT64});
};

std::ostream &operator<<(std::ostream &, const GradInOutMapper &);
std::ostream &operator<<(std::ostream &, const GradOpInType &);

// A note on non-determinism. For maps with
// pointers as keys, iterating through them
// is non-deterministic with the default comparator.
// To prevent non-determinism, POpCmp is used on any sets and maps that use
// pointers to operators as a set/map key.
struct POpCmp {
  bool operator()(Op *const &a, Op *const &b) const { return a->id < b->id; }
};

} // namespace popart

#endif
