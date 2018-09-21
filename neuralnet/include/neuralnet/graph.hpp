#ifndef GUARD_NEURALNET_GRAPH_HPP
#define GUARD_NEURALNET_GRAPH_HPP

#pragma clang diagnostic push // start ignoring warnings
#pragma clang diagnostic ignored "-Weverything"
// The protobuf generated ONNX classes
#include <onnx/onnx.pb.h>
#pragma clang diagnostic pop // stop ignoring warnings

#include <map>
#include <neuralnet/names.hpp>
#include <neuralnet/tensorinfo.hpp>

namespace neuralnet {

class Tensor;
class Graph;
class Op;

// if the GraphProto of the ModelProto argument
// has a single output, return the outputs name,
// otherwise throw an error
TensorId getUniqueOutId(const onnx::ModelProto &m);


// The gradient of a tensor is the sum of 1 or several tensors, 
// 1 for each of the nodes which consumed it. This class is for
// tracking/counting these as they come in down edges.
class TensorGradRegistry{
public:
  using TMap = std::map<Tensor *, std::vector<Tensor *>>;
  // Register tensor "grad" as being a 
  // gradient of "nonGrad" w.r.t. loss along some edge
  void insert(Tensor * nonGrad, Tensor * grad);

  // Return the non-gradient tensors which have ALL their required gradients
  // registed, and are thus ready to have their edge gradients summed to 
  // obtain the final gradient.
  // NOT a const member function
  TMap popComplete();

private:
  // stores all non-grad tensors which have some but not all of
  // their edges having provided gradients
  TMap partial;
  // stores all non-grad tensors which have had all of their
  // edges provide gradients. When popCompleted() is called, 
  // this map is returned,
  TMap complete;

};

class OpGradRegistry{
public:
  using NMap = std::map<Op *, std::map<int, Tensor *>>;
  void insert(Op * nonGrad, int index, Tensor * grad);
  NMap popComplete();

  private:
  // For a non-grad-op, which input gradient have been computed
   NMap partial;
  // When all required gradient inputs are in, move from partial to complete
   NMap complete;

};

class OpAndIndices {

public:

  // this way or the other way round, the map?
  OpAndIndices(std::unique_ptr<Op> gradOp_,
               const std::map<int, int> &forwards_in_to_backwards_out);

  OpAndIndices() = default;

  // Let the non-gradient Op be fwdOp.
  // then this is one of fwdOp's gradient Ops (which
  // computes the gradients of one or several of 
  // fwdOp's inputs):
  std::unique_ptr<Op> gradOp;

  int getForwardIndex(int backwardIndex);
  int getBackwardIndex(int forwardIndex);

  private:

  // the keys are the indices of input tensor to the
  // forward Op, with the values being the indices at which
  // this gradOp outputs their gradient
  std::map<int, int> forwardInToBackOut;

  // the opposite,
  std::map<int, int> backOutToForwardIn;
};

using OpsAndIndices = std::vector<OpAndIndices>;

// Tensors to log every iteration
// Also, frequency at which to return all weights
// TODO(jn) ask David Norman how tensorflow does this.
class Recorder {};

std::string getNeuralNetDomain();

class Loss {
public:
  Loss()          = default;
  virtual ~Loss() = default;

  // (1) set opId and pgraph (private class members). 
  //     This can't be done at construction
  //     time as they are not know at that point.
  //     Also, set input and output (same format as a Node : ""
  //     represents no input at an index).
  // (2) return the Loss Op.
  std::unique_ptr<Op> finalSetAndGetOp(Graph *pgraph);

  // the names of all the tensors which will be streamed into the
  // Op this Loss generates. For NLL, it is the label tensor. For
  // MSE it is the target tensor. There may be several such streamed
  // tensors.
  virtual std::vector<TensorId> getStreamTensorNames() const = 0;

  // The name of the Loss Op
  virtual TensorId getLossId() const = 0;

  // The op_type string which the Op which this node
  // generates should have.
  virtual std::string op_type() const = 0;

  OpId getOpId() const;
  Graph *getGraph() const;

  const TensorId &input(int i) const;
  int input_size() const;
  const TensorId &output(int i) const;
  int output_size() const;

private:
  // The OpId of the Op this Loss will generate
  OpId opId{-1};
  // The Graph ofo the Op that this loss will generate
  Graph *pgraph{nullptr};

  // The names of the input tensors
  std::vector<TensorId> input_;
  // The names of the output tensors
  std::vector<TensorId> output_;

  // Rule: output_.size() should be input_.size() + 1.
  //     : In particular, let input_.size() = N.
  //       The first N outputs are the gradients of the N
  //       inputs, in order. Stream tensors included!
  //       Label interpreted as probability vector for grad.
  virtual void setInOut(std::vector<TensorId> &,
                        std::vector<TensorId> &) const = 0;

  // return the Op for this Loss, should
  // only be called from getOp()
  virtual std::unique_ptr<Op> getSpecificOp() const = 0;
};

// where tensor tenId is consumed by op opId at index index,
// what should the name of the gradient along this edge? 
// This is purely string manipulation.
TensorId getGradId(TensorId tenId, OpId opId, int index);

// the name of the tensor of the total gradient (loss and regularizers)
TensorId getGradId(TensorId tenId);

// needs to be implemented. will manage things like
// weight decay loss etc.
class Regularizer {};

// Learning scheduler
// momentum, learning rates, etc.
class Schedule {};

// What is known about the Graph before it is run.
// This knowledge can sometimes be compiled into the Graph,
// and for certain backends is even required, for example
// Graphcore IPU requires all Stream Tensor shapes.
class PreRunKnowledge {
public:
  PreRunKnowledge() = default;
  void addInfo(TensorId, const TensorInfo &);
  const TensorInfo &getInfo(TensorId) const;
  bool hasInfo(TensorId) const;
  const std::map<TensorId, TensorInfo> &getInfos() const;

  // return all unique TensorIds of tensors with any
  // information stored in this object, be it TensorInfo
  // or actual tensor.
  std::vector<TensorId> getAllTensorIds() const;

private:
  std::map<TensorId, TensorInfo> infos;
  // we will also have a map of actual tensors, these
  // can be used sometimes to compile the graph (slice
  // indices for example)
};

enum class OpType {
  AVERAGEPOOL = 0,
  AVERAGEPOOLGRAD,
  CONSTANT,
  CONV,
  LOGSOFTMAX,
  NEGLOGLIKE,
  PAD,
  RELU,
  SUM,
};


// models inputs and outputs to Ops, inputs/outputs 
// enter/leave at certain indices of an Op
// 1 tensor per index, but 1+ index per tensor
class TensorIndexMap {
public:
  void insert(int, Tensor *);
  // the Tensor at index changes. Note that there
  // must already be a Tensor at the index
  void reset(int, Tensor *);
  Tensor *tensor(int);
  const Tensor *tensor(int) const;
  bool hasIndex(int) const;
  const std::vector<int> &indices(Tensor *) const;
  const std::map<Tensor *, std::vector<int>> &indicesMap() const;
  const std::map<int, Tensor *> &tensorMap() const;
  // the number or indices (keys of tensor_map)
  int n() const;
  void append(std::stringstream &, std::string prefix) const;
  // set the TensorInfo of tensor(index) if hasIndex(index) is true
  void setInfoIfIndex(const TensorInfo &, int index);
  // the returned vector has correct TensorIds at indices in
  // tensor_map and "" at unused indices inbetween
  std::vector<TensorId> getSerialised() const;

private:
  std::map<int, Tensor *> tensor_map;
  std::map<Tensor *, std::vector<int>> indices_map;
};


// Wrapper around the container of onnx::AtrributeProtos
// of a Node, provides faster and cleaner reads of values 
// from keys (strings)
class Attributes {
public:
  Attributes(decltype(onnx::NodeProto().attribute()) &);
  Attributes() = default;
  const std::vector<std::string> &getNames() const;
  onnxAttPtr at(std::string name) const;
  void append(std::stringstream &ss) const;
  template <typename T> void setIfPresent(T &, std::string s) const;

private:
  std::map<std::string, onnxAttPtr> att_map;
  std::vector<std::string> names;
};

template <> void Attributes::setIfPresent(int64_t &, std::string s) const;

template <>
void Attributes::setIfPresent(std::vector<int64_t> &, std::string s) const;

template <> void Attributes::setIfPresent(std::string &, std::string s) const;

class OpConstructorBundle {
public:
  OpConstructorBundle(std::string op_type,
                      Graph *,
                      Attributes,
                      std::string domain);

  std::string op_type;
  Graph *pgraph;
  Attributes atts;
  std::string domain;
};

class Op {
public:
  Op(const Node &, Graph *);
  Op(const OpConstructorBundle &);

  // create an ActGrad (output) tensor
  // and wire it to this Ops output
  void createAndConnectOutTensor(OutIndex, TensorId);

  void append(std::stringstream &ss) const;
  virtual ~Op();

  // The consumed Tensors
  TensorIndexMap input;

  // The produced Tensors
  TensorIndexMap output;

  // wire a tensor to input: updates input and
  // updates consumers of tensor with id TensorId
  void connectInTensor(InIndex, TensorId);

  // might the input tensors be modified?
  bool mayModify(InIndex) const;

  // all Ops will be performed "as close to" the order of 
  // priority (highest to lowest) while still being topo sorted.
  // This is not finalised, might work differently...
  double priority() const;

  // "Relu" or "Conv" etc.
  const std::string &op_type() const;
  const OpType opType;

  // political affiliation of the Op (same as NodeProto)
  const std::string &domain();

  // the graph to which the Op belongs
  Graph *pgraph;

  // The unique identifier of the Op (will always be set in Op::Op)
  OpId id{-1};

  // attributes from the Node, if it was created from one
  const Attributes nAtts;

  // set shape and type parameters,
  // MUST set output TensorInfos for all outputs
  virtual void setup();

  // return a vector of 1 or several OpAndTensorIds: for
  // obtaining the gradient of the inputs of this Op.
  // The Op in a OpAndTensorId is a gradient op, and
  // the TensorIds map this Op's input indices to the
  // the output indices of the gradient Op where the 
  // corresponding gradient is output
  // If this Op is already a gradient Op, throws error
  virtual OpsAndIndices getGradOps() const;

  // return a gradient op's non-gradient partner if relevant,
  // otherwise throws an error
  virtual Op *getNonGradOp() const;

  // what input index of a nonGradOp does 
  // part gradient coming out at index partGradInd
  // correspond to? If not relevant, throw an error
  virtual int getNonGradInIndex(int partGradInd) const;

  virtual bool readyToCreateGradients(std::map<int, Tensor *> &) const;

private:
  void appendIO(std::stringstream &) const;
  virtual void appendMore(std::stringstream &) const {}
  const std::string *const p_op_type;
  std::string op_domain;

  // design decision : see-sawing between storing a pointer
  // to the Node from which the Op derives (if it does derive
  // from a Node) or not. Deciding not to for now, (1) not much 
  // to copy to the Op (2) cleaner 

  // design decision : see-sawing between having special classes
  // for NonGradOp and GradOp, deciding not to. The main motivation
  // for HAVING the distinction was that inputs of GradOps would 
  // work differently, that instead of listing them all, the 
  // non-grad inputs would be implit from the corresponding 
  // Op. Also, there could be functions like "getNonGrapOp" 
  // which would return the NonGradOp for a GradOp.
  // Motivation for implicit input was the explicit:
  // 1) inefficient.
  // 2) if done in the same dimensions (ie concat the inputs), how to
  //    handle variadic input size? (ie SumOp).

  // rebuttal to
  // 1) not valid (a few more strings?) and also constricts
  //    the grad op to always take all inputs and outputs
  //    from non-grad op
  // 2) not sure what the problem is here. variadic inputs can be
  //    interleaved if they are of the same size
};

enum class TensorType;

class OpTypes {
public:
  OpTypes();
  const OpType &get(std::string op_type) const;
  const std::string &get(OpType opType) const;

private:
  std::map<std::string, OpType> opTypes_;
  std::map<OpType, std::string> strings_;
};

OpTypes initOpTypes();
const OpTypes &getOpTypes();

class VectorAndSet {
public:
  VectorAndSet(std::vector<std::string> &&vals);
  bool contains(std::string) const;
  const std::vector<std::string> &v() const;
  ~VectorAndSet();

private:
  std::vector<std::string> v_vals;
  std::set<std::string> m_vals;
};

std::string reservedPrefix();

class Tensors {
public:
  Tensors(std::vector<std::string> &&vals1, Graph *pg);
  ~Tensors();
  // Store the Tensors of type Const
  const VectorAndSet constIds;
  Tensor *get(TensorId) const;
  void remove(TensorId);
  bool contains(TensorId) const;
  // create a Tensor, either of type Const or Variable
  void addInit(TensorId, const onnx::TensorProto *);
  // create a Tensor of type Stream
  void addStream(TensorId);
  // create a Tensor of type ActGrad (basically any tensor which is
  // the output of an Op)
  void addActGrad(TensorId);
  std::vector<TensorId> getInitIds() const;
  std::vector<TensorId> getIds(TensorType) const;
  std::vector<TensorId> getNoProducerIds() const;
  const onnx::TensorProto *getOnnxInit(TensorId) const;
  void addNonGradient(TensorId gradId, Tensor *nonGradTensor);

  // return the tensor of which the
  // tensor with TensorId is a COMPLETE gradient
  Tensor *getNonGradientOf(TensorId) const;

private:
  std::map<TensorId, std::unique_ptr<Tensor>> M;
  OnnxTensorPtrs init;
  Graph *pgraph;

  // from gradients to non-gradients (if there are any)
  std::map<TensorId, Tensor *> non_gradients_;
};

class Graph {
public:
  Graph(onnx::ModelProto &&,
        PreRunKnowledge &&,
        Recorder &&,
        std::unique_ptr<Loss> &&,
        std::vector<std::unique_ptr<Regularizer>> &&,
        // Schedule needed, if momentum the graph is different
        Schedule &&sched,
        // Weights tensors which are not to be updated
        std::vector<std::string> &&cTens);

  // take training steps
  onnx::ModelProto step(int n);
  // if the tensor is returned to user (Recorder).
  bool isLogged(TensorId);
  void append(std::stringstream &);
  PreRunKnowledge preRunKnowledge;
  Recorder recorder;
  std::unique_ptr<Loss> loss;
  std::vector<std::unique_ptr<Regularizer>> regularizers;
  Schedule schedule;
  Tensors tensors;
  ~Graph();
  // split ConvOp with bias into two Ops, a ConvOp
  // followed by an x Op
  void splitConvBias();
  // Padding with edges of width 0 is a nop,
  // remove it unless logging tensors prevents
  void removePadSizeZero();
  // remove []->() where [] is Tensor and () is an Op and []->()
  // forms part of (.)->[]->()->[.]. after this, this section will
  // be (.)->[.]
  void removeNullOp(TensorId name, OpId opId);
  // return pointers to Ops of a certain type
  std::vector<Op *> opsOfType(OpType);
  void inferTensorInfos();
  // this does not take into priority, simple topological sort
  std::vector<Op *> getTopologicallySorted() const;

  void constructForwards();
  void constructBackwards();
  OpId getOpsCounter() const;
  OpId getAndIncrOpsCounter();

private:
  // confirm that the names of the Const tensors
  // from the user (constTensors) are in the onnx Model
  // Can be run after the forward pass of Graph has been
  // constructed
  void confirmConstIds() const;

  // gradients are named automatically. To prevent them
  // getting names already taken by non-gradiet tensors,
  // we check that a reserved pattern is not present.
  void confirmNonGradId(TensorId tenId) const;

  // cofirm that no tensors in input(), nodes() or preRunKnowlede()
  // use reserved naming conventions. A note on design: The decision
  // to NOT add an independent dimension to TensorId, used exclusively
  // by automatically named tensors, was that when printing TensorIds
  // there would still be the possibility of conflict (i.e. projection
  // to single string might result in conflict).
  void confirmNoGradIds() const;

  // create an Op from Node (if not Constant Node), wire it to
  // correct input Tensors and create the activation output Tensors
  Op *growFromNode(const Node &);

  // create an Op from loss, and wire it to the correct input Tensors,
  // and create the activate output Tensor(s)
  Op *growFromLoss();

  Op *growGradSumOp(Tensor *target, const std::vector<Tensor *> &toSum);

  std::vector<Op *> growGradOps(Op *forwardOp,
                                const std::map<int, Tensor *> &gradientsIn);

  // called from growFromNode and growFromLoss.
  // T requires functions input(int) and input_size()
  template <typename T> void connectInputs(const T &, OpId opId);
  // T requires functions output(int) and output_size()
  template <typename T> void connectOutputs(const T &, OpId opId);

  const onnx::ModelProto onnxModel;

 // // Nodes created during the building of the graph, includes
 // // Nodes for the backwards pass.
 // std::vector<std::unique_ptr<Node>> constructedNodes;

  // create an Op from a Node
  std::unique_ptr<Op> addOp(const Node &);
  std::map<OpId, std::unique_ptr<Op>> ops;

  // moves ownsership of created Op into the Graph, 
  // and returns the Op's OpId (which it already has)
  OpId moveIntoGraph(std::unique_ptr<Op> op);

  void registerOpGrads(Op *);
  void registerTensorGrad(Tensor *);

  TensorGradRegistry tensor_grad_registry;
  OpGradRegistry op_grad_registry;


  // total number of ops ever created
  OpId opsCounter{0};
};

} // namespace neuralnet

#endif
