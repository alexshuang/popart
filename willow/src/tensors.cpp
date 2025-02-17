// Copyright (c) 2019 Graphcore Ltd. All rights reserved.
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <onnxutil.hpp>
#include <ostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <poplar/Target.hpp>
#include <popart/graph.hpp> // IWYU pragma: keep
#include <popart/ir.hpp>
#include <popart/names.hpp>
#include <popart/sessionoptions.hpp>
#include <popart/tensor.hpp>
#include <popart/tensordebuginfo.hpp>
#include <popart/tensors.hpp>
#include <popart/variablesettings.hpp>
#include <popart/voiddata.hpp>

#include "popart/dataflow.hpp"
#include "popart/datatype.hpp"
#include "popart/debugcontext.hpp"
#include "popart/error.hpp"
#include "popart/logging.hpp"
#include "popart/pointercomparators.hpp"
#include "popart/scope.hpp"
#include "popart/tensorinfo.hpp"
#include "popart/util.hpp"
#include "popart/vectorandset.hpp"

namespace onnx {

class TensorProto;
} // namespace onnx

namespace popart {

TensorId Tensors::moveIntoTensors(std::unique_ptr<Tensor> tensor) {
  auto id = tensor->id;
  insert(id, std::move(tensor));
  return id;
}

std::vector<TensorId> Tensors::getAllTensorIds() const {
  std::vector<TensorId> allIds;
  allIds.reserve(M.size());
  for (auto &id_tensor : M) {
    allIds.push_back(id_tensor.first);
  }
  return allIds;
}

std::vector<Tensor *> Tensors::getAll() const {
  std::vector<Tensor *> tensors;
  for (auto &id_pt : M) {
    tensors.push_back(id_pt.second.get());
  }
  // Sort the vector by id to return a deterministic list.
  std::sort(tensors.begin(), tensors.end(), PTensorCmp());
  return tensors;
}

// remove all Tensors with no producer and no consumers
void Tensors::removeIsolated(bool retainUsedIOTensors,
                             bool retainAllIOTensors,
                             bool retainVarTensors,
                             bool retainConstTensors) {
  auto hostLoadTensors = graph.getIr().getHostLoadTensors();
  for (auto &id : getAllTensorIds()) {
    Tensor *tensor = M[id].get();
    if (tensor->hasProducer() == false && tensor->consumers.getTotal() == 0) {
      bool isUsedIoTensor =
          tensor->tensorLocationInfo.isRemote() || tensor->isAnchored() ||
          tensor->isRootAnchor() ||
          (tensor->tensorType() == TensorType::Stream &&
           hostLoadTensors.find(tensor->id) != hostLoadTensors.end());
      bool isAllIOTensor =
          isUsedIoTensor && (tensor->tensorType() == TensorType::Stream);
      bool isVarTensor   = tensor->tensorType() == TensorType::Variable;
      bool isConstTensor = tensor->tensorType() == TensorType::Const;
      if (!(retainUsedIOTensors && isUsedIoTensor) &&
          !(retainAllIOTensors && isAllIOTensor) &&
          !(retainVarTensors && isVarTensor) &&
          !(retainConstTensors && isConstTensor)) {
        // Note: we must log before the erase to avoid reading invalid memory.
        logging::ir::debug(
            "Removing isolated Tensor::{} {}", tensor->tensor_type(), id);
        M.erase(id);
      }
    }
  }
}

std::vector<Tensor *> Tensors::getOfType(TensorType type) const {
  std::vector<Tensor *> ofType;
  for (auto &id_pt : M) {
    if (id_pt.second->tensorType() == type) {
      ofType.push_back(id_pt.second.get());
    }
  }
  // Sort the vector by id to return a deterministic list.
  std::sort(ofType.begin(), ofType.end(), PTensorCmp());
  return ofType;
}

std::vector<Tensor *>
Tensors::getOfType(const std::vector<TensorType> &tTypes) const {
  std::vector<Tensor *> ofType;
  for (auto type : tTypes) {
    auto ofTypesTemp = getOfType(type);
    ofType.insert(ofType.end(), ofTypesTemp.cbegin(), ofTypesTemp.cend());
  }
  return ofType;
}

std::vector<TensorId> Tensors::getIds(TensorType type) const {
  auto typedTensors = getOfType(type);
  std::vector<TensorId> ids;
  ids.reserve(typedTensors.size());
  for (Tensor *t : typedTensors) {
    ids.push_back(t->id);
  }
  return ids;
}

Tensors::Tensors(Graph &pg) : graph(pg) {}

Tensor *Tensors::get(TensorId tenId) const {
  auto found = M.find(tenId);
  if (found == M.end()) {
    throw error("No Ir::Tensor with TensorId '" + tenId +
                "' in Tensors::get(..)");
  }
  return found->second.get();
}

bool Tensors::contains(TensorId tenId, const Scope &scope) const {
  Scope s = scope;

  while (!s.empty()) {
    auto id = (s / tenId).str();
    if (M.find(id) != M.end()) {
      return true;
    } else {
      s.pop();
    }
  }

  if (M.find(tenId) != M.end()) {
    return true;
  } else {
    return false;
  }
}

TensorId Tensors::find(TensorId tenId, const Scope &scope) const {
  Scope s = scope;

  while (!s.empty()) {
    auto id = (s / tenId).str();
    if (M.find(id) != M.end()) {
      return id;
    } else {
      s.pop();
    }
  }

  if (M.find(tenId) != M.end()) {
    return tenId;
  } else {
    throw error("Could not find tensor with id {} in scope {}", tenId, scope);
  }
}

void Tensors::append(std::stringstream &ss) const {
  bool frst = true;
  ss << '[';
  for (auto &id_ptr : M) {
    if (!frst) {
      ss << ' ';
    }
    frst = false;
    ss << id_ptr.first;
  }
  ss << ']';
}

std::vector<TensorId> Tensors::getNoProducerIds() const {
  // the tensors which are not generated by an Op
  std::vector<TensorId> t0 = getIds(TensorType::Stream);
  std::vector<TensorId> t1 = getIds(TensorType::Const);
  std::vector<TensorId> t2 = getIds(TensorType::Variable);
  t0.insert(t0.end(), t1.begin(), t1.end());
  t0.insert(t0.end(), t2.begin(), t2.end());
  return t0;
}

void Tensors::insert(TensorId name, std::unique_ptr<Tensor> t) {
  if (M.find(name) != M.end()) {
    throw internal_error("tensor {} already in M", name);
  }
  M[name] = std::move(t);
}

void Tensors::addConstInit(const TensorId &name,
                           const ONNX_NAMESPACE::TensorProto *pt,
                           const DebugContext &debugContext) {
  popart::TensorDebugInfo di(debugContext, name, TensorType::Const);
  addInit(name, pt, TensorType::Const, di);
  insertConstId(name);
}

void Tensors::addVarInit(const TensorId &name,
                         const ONNX_NAMESPACE::TensorProto *pt,
                         const DebugContext &debugContext) {
  addVarInit(name, pt, VariableSettings(), debugContext);
}

void Tensors::addVarInit(const TensorId &name,
                         const TensorInfo &info,
                         const void *src,
                         const DebugContext &debugContext) {
  addVarInit(name, info, src, VariableSettings(), debugContext);
}

void Tensors::addVarInit(const TensorId &name,
                         const ONNX_NAMESPACE::TensorProto *pt,
                         const VariableSettings &vs,
                         const DebugContext &debugContext) {
  logging::debug("Adding VarInit Tensor {}", name);
  popart::TensorDebugInfo di(debugContext, name, TensorType::Variable);
  addInit(name, pt, TensorType::Variable, vs, di);
}

void Tensors::addVarInit(const TensorId &name,
                         const TensorInfo &info,
                         const void *src,
                         const VariableSettings &vs,
                         const DebugContext &debugContext) {

  auto [init, nbytes] = addVarInitCore(name, info, vs, debugContext);
  init->setTensorDataFromCopyOf(src, nbytes);
}

void Tensors::addVarInitFromViewOf(const TensorId &name,
                                   const TensorInfo &info,
                                   void *src,
                                   const VariableSettings &vs,
                                   const DebugContext &debugContext) {
  auto [init, nbytes] = addVarInitCore(name, info, vs, debugContext);
  init->setTensorDataFromViewOf(src, nbytes);
}

std::tuple<Tensor *, unsigned>
Tensors::addVarInitCore(const TensorId &name,
                        const TensorInfo &info,
                        const VariableSettings &vs,
                        const DebugContext &debugContext) {
  popart::TensorDebugInfo di(debugContext, name, info, TensorType::Variable);
  logging::devicex::debug("AddVarInit.info {}, {}", name, info.shape());
  insert(name, std::unique_ptr<Tensor>(new Tensor(name, vs, graph, di)));

  Tensor *init           = get(name);
  init->info             = info;
  Shape shape_on_host    = info.shape();
  Shape shape_on_replica = vs.shapeOnReplica(
      shape_on_host,
      graph.getIr().getSessionOptions().getGlobalReplicationFactor(),
      name);

  init->info = TensorInfo(info.dataType(), shape_on_replica);

  logging::debug(
      "addVarInit({}) --({})-->({})", name, init->info.shape(), shape_on_host);
  TensorInfo ex_info(info.dataType(), shape_on_host);

  return std::make_pair(init, ex_info.nbytes());
}

void Tensors::addConstInit(const TensorId &name,
                           const TensorInfo &info,
                           const void *src,
                           const DebugContext &debugContext) {
  popart::TensorDebugInfo di(debugContext, name, info, TensorType::Const);
  insert(name, std::make_unique<Tensor>(name, TensorType::Const, graph, di));

  insertConstId(name);

  Tensor *init = get(name);
  init->info   = info;
  init->setTensorDataFromCopyOf(src, info.nbytes());
}

void Tensors::makeConstInit(const TensorId &name, const void *src) {
  insertConstId(name);

  auto *tensor = get(name);
  if (tensor->hasProducer()) {
    throw error("cannot make an existing tensor const if it has a producer");
  }
  tensor->setTensorType(TensorType::Const);
  tensor->setTensorDataFromCopyOf(src, tensor->info.nbytes());
}

void Tensors::addInit(const TensorId &name,
                      const ONNX_NAMESPACE::TensorProto *pt,
                      TensorType tt,
                      const DebugInfo &di) {
  addInit(name, pt, tt, VariableSettings(), di);
}

namespace {
TensorData TensorDataFromOnnxProto(const ONNX_NAMESPACE::TensorProto &tp) {
  ConstVoidData cv_data = onnxutil::getConstData(tp);
  // Would be able to emplace here if ConstVoidData was stored as a vector<char>
  // instead of raw pointer.
  return TensorData::fromCopyOf(cv_data.data, cv_data.info.nbytes());
}
} // namespace

void Tensors::addInit(const TensorId &name,
                      const ONNX_NAMESPACE::TensorProto *pt,
                      TensorType tt,
                      const VariableSettings &vs,
                      const DebugInfo &di) {
  if (tt == TensorType::Variable) {
    insert(name, std::make_unique<Tensor>(name, vs, graph, di));
  } else {
    insert(name, std::make_unique<Tensor>(name, tt, graph, di));
  }

  // make sure the info shape match the shape it should have on the graph, and
  // not the total data unit
  Tensor *init    = get(name);
  TensorInfo info = TensorInfo(*pt);
  init->info      = TensorInfo(
      info.dataType(),
      vs.shapeOnReplica(
          info.shape(),
          graph.getIr().getSessionOptions().getGlobalReplicationFactor(),
          name));
  init->setTensorData(TensorDataFromOnnxProto(*pt));
}

void Tensors::addStream(TensorId tenId,
                        const TensorInfo &info,
                        const DebugContext &debugContext) {
  addStream(tenId, info, {}, debugContext);
}

void Tensors::addStream(TensorId tenId,
                        const TensorInfo &info,
                        const InputSettings &settings,
                        const DebugContext &debugContext) {
  popart::TensorDebugInfo di(debugContext, tenId, info, TensorType::Stream);
  insert(tenId,
         std::unique_ptr<Tensor>(
             new Tensor(tenId, TensorType::Stream, graph, di)));
  Tensor *t        = get(tenId);
  t->info          = info;
  t->inputSettings = settings;
}

void Tensors::addActGrad(TensorId tenId, const DebugContext &debugContext) {
  popart::TensorDebugInfo di(debugContext, tenId, TensorType::ActGrad);
  logging::debug("Adding ActGrad Tensor {}", tenId);
  insert(tenId,
         std::unique_ptr<Tensor>(
             new Tensor(tenId, TensorType::ActGrad, graph, di)));
}

void Tensors::remove(TensorId id) { M.erase(id); }

bool Tensors::contains(TensorId id) const { return M.find(id) != M.end(); }

void Tensors::insertConstId(const std::string &id) { constIds.insert(id); }

void addConstInitFromFloat(float value,
                           const TensorId &valueId,
                           const TensorInfo &tensorInfo,
                           Tensors &tensors) {

  switch (tensorInfo.dataType()) {
  case DataType::FLOAT: {
    std::vector<float> gradStarterData(1, value);
    tensors.addConstInit(
        valueId, tensorInfo, reinterpret_cast<void *>(gradStarterData.data()));
    break;
  }
  case DataType::FLOAT16: {
    std::vector<float> floatData(1, value);
    std::vector<char> gradStarterData(2);
    poplar::copyFloatToDeviceHalf(
        poplar::Target(), floatData.data(), gradStarterData.data(), 1);
    tensors.addConstInit(
        valueId, tensorInfo, reinterpret_cast<void *>(gradStarterData.data()));
    break;
  }
  case DataType::INT16: {
    std::vector<int16_t> gradStarterData(1, static_cast<int16_t>(value));
    tensors.addConstInit(
        valueId, tensorInfo, reinterpret_cast<void *>(gradStarterData.data()));
    break;
  }
  case DataType::INT32: {
    std::vector<int32_t> gradStarterData(1, static_cast<int32_t>(value));
    tensors.addConstInit(
        valueId, tensorInfo, reinterpret_cast<void *>(gradStarterData.data()));
    break;
  }
  case DataType::INT64: {
    std::vector<int64_t> gradStarterData(1, static_cast<int64_t>(value));
    tensors.addConstInit(
        valueId, tensorInfo, reinterpret_cast<void *>(gradStarterData.data()));
    break;
  }
  case DataType::UINT32: {
    std::vector<uint32_t> gradStarterData(1, static_cast<uint32_t>(value));
    tensors.addConstInit(
        valueId, tensorInfo, reinterpret_cast<void *>(gradStarterData.data()));
    break;
  }
  case DataType::UINT64: {
    std::vector<uint64_t> gradStarterData(1, static_cast<uint64_t>(value));
    tensors.addConstInit(
        valueId, tensorInfo, reinterpret_cast<void *>(gradStarterData.data()));
    break;
  }
  // Making it explicit which data types we're not handling. Note that
  // the logic will fall through to the error.
  case DataType::UINT8:
  case DataType::INT8:
  case DataType::UINT16:
  case DataType::BOOL:
  case DataType::BFLOAT16:
  case DataType::DOUBLE:
  case DataType::COMPLEX64:
  case DataType::COMPLEX128:
  case DataType::STRING:
  case DataType::UNDEFINED:
  default: {
    throw error("Unexpected data-type, '{}'",
                tensorInfo.getDataTypeInfo()->name());
  }
  }
}

} // namespace popart
