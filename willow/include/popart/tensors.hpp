// Copyright (c) 2019 Graphcore Ltd. All rights reserved.
#ifndef POPART_WILLOW_INCLUDE_POPART_TENSORS_HPP_
#define POPART_WILLOW_INCLUDE_POPART_TENSORS_HPP_

#include <cstddef>
#include <iosfwd>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>
#include <popart/variablesettings.hpp>
#include <popart/vectorandset.hpp>

#include "popart/dataflow.hpp"
#include "popart/debugcontext.hpp"
#include "popart/tensor.hpp"
#include "popart/tensordebuginfo.hpp"

namespace onnx {
class TensorProto;
} // namespace onnx

namespace popart {
class Graph;
class Scope;
class TensorInfo;

class Tensors {
public:
  Tensors(Graph &pg);
  ~Tensors() = default;

  Tensor *get(TensorId) const;
  void remove(TensorId);
  bool contains(TensorId) const;

  std::size_t n() const { return M.size(); }

  // Search for a tensor with a scope
  // Return the scoped tensorId
  TensorId find(TensorId, const Scope &) const;
  // Search for a tensor with a scope
  bool contains(TensorId, const Scope &) const;

  // create a Variable Tensor
  void addVarInit(const TensorId &,
                  const ONNX_NAMESPACE::TensorProto *,
                  const DebugContext &dc = {});
  void addVarInit(const TensorId &,
                  const TensorInfo &,
                  const void *,
                  const DebugContext &dc = {});

  void addVarInit(const TensorId &,
                  const ONNX_NAMESPACE::TensorProto *,
                  const VariableSettings &,
                  const DebugContext &dc = {});
  void addVarInit(const TensorId &,
                  const TensorInfo &,
                  const void *,
                  const VariableSettings &,
                  const DebugContext &dc = {});
  void addVarInitFromViewOf(const TensorId &,
                            const TensorInfo &,
                            void *,
                            const VariableSettings &,
                            const DebugContext &dc = {});

  // create a Constant Tensor
  void addConstInit(const TensorId &,
                    const ONNX_NAMESPACE::TensorProto *,
                    const DebugContext &dc = {});
  void addConstInit(const TensorId &,
                    const TensorInfo &,
                    const void *,
                    const DebugContext &dc = {});

  // make an existing tensor a const init tensor
  void makeConstInit(const TensorId &, const void *);

  // create a Tensor of type Stream
  void addStream(TensorId, const TensorInfo &, const DebugContext &dc = {});

  // create a Tensor of type Stream
  void addStream(TensorId,
                 const TensorInfo &,
                 const InputSettings &,
                 const DebugContext &dc = {});
  // create a Tensor of type ActGrad (basically any tensor which is
  // the output of an Opm)
  void addActGrad(TensorId, const DebugContext &dc = {});
  std::vector<TensorId> getIds(TensorType) const;
  std::vector<Tensor *> getAll() const;
  std::vector<Tensor *> getOfType(TensorType) const;
  std::vector<Tensor *> getOfType(const std::vector<TensorType> &) const;
  std::vector<TensorId> getAllTensorIds() const;
  std::vector<TensorId> getNoProducerIds() const;
  void append(std::stringstream &) const;

  const VectorAndSet<TensorId> &getConstIds() const { return constIds; }
  void insertConstId(const std::string &);
  // remove all Tensors which have no producer and no consumers
  void removeIsolated(bool retainUsedIOTensors = false,
                      bool retainAllIOTensors  = false,
                      bool retainVarTensors    = false,
                      bool retainConstTensors  = false);

  TensorId moveIntoTensors(std::unique_ptr<Tensor> tensor);

private:
  // Store the Tensors of type Const
  VectorAndSet<TensorId> constIds;

  std::unordered_map<TensorId, std::unique_ptr<Tensor>> M;
  // adds to M, but first confirms that TensorId not already in
  void insert(TensorId, std::unique_ptr<Tensor>);

  void addInit(const TensorId &,
               const ONNX_NAMESPACE::TensorProto *,
               TensorType,
               const DebugInfo &di);

  void addInit(const TensorId &,
               const ONNX_NAMESPACE::TensorProto *,
               TensorType,
               const VariableSettings &,
               const DebugInfo &di);

  std::tuple<Tensor *, unsigned> addVarInitCore(const TensorId &name,
                                                const TensorInfo &info,
                                                const VariableSettings &vs,
                                                const DebugContext &dc);

  Graph &graph;
};

/**
 * Helper to adds const init tensor to tensors.
 * \param value float value of tensor to be added to \a tensors.
 * \param valueId tensor id of tensor to be added to \a tensors.
 * \param tensorInfo tensor info of tensor to be added to \a tensors.
 * \param tensors \a Tensors to which we add the tensor.
 */
void addConstInitFromFloat(float value,
                           const TensorId &valueId,
                           const TensorInfo &tensorInfo,
                           Tensors &tensors);

} // namespace popart

#endif // POPART_WILLOW_INCLUDE_POPART_TENSORS_HPP_
