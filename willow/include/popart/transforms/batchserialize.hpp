// Copyright (c) 2020 Graphcore Ltd. All rights reserved.
#ifndef GUARD_NEURALNET_BATCHSERIALIZE_HPP
#define GUARD_NEURALNET_BATCHSERIALIZE_HPP

#include <cstddef>
#include <string>
#include <vector>
#include <popart/op.hpp>
#include <popart/shardingplan.hpp>
#include <popart/transforms/transform.hpp>

#include "popart/basicoptionals.hpp"
#include "popart/names.hpp"
#include "popart/tensorinfo.hpp"

// Batch serialization:
// Serializes Ops in the forward pass along the batch dimension,
// starting from data inputs. The data/activation path is sliced along the
// batch dimension, and concatenated again for ops that do not support batch
// serialization, such as Losses and BatchNorm.
// Crossing boundaries such as ExecutionPhases, VirtualGraphs and Pipeline
// stages also causes the batch to be concatenated again.
// The backward pass is grown as normal, but the second batch serialization pass
// will look for local graph isomorphisms to ensure each batch serialized
// sequence (for each batch) is scheduled identically when possible,
// which improves the outlining outcome.
//
// Before transformation:
//           w0                          w1
//           |                           |
//   data - MatMul - ReLU - BatchNorm - MatMul - Loss
//
// After transformation (batch serialization factor 4):
//
//        data [batch(4), c, h, w]
//          |
//          +-----------------+-----------------+-----------------+
//          |                 |                 |                 |
//       DynamicSlice(0)   DynamicSlice(1)   DynamicSlice(2)   DynamicSlice(3)
//          |                 |                 |                 |
//    w0 - MatMul       w0 - MatMul       w0 - MatMul       w0 - MatMul
//          |                 |                 |                 |
//         ReLU              ReLU              ReLU              ReLU
//          |                 |                 |                 |
// Init- DynamicUpdate(0)- DynamicUpdate(1)- DynamicUpdate(2)- DynamicUpdate(3)
//                                                                |
//                                                              BatchNorm
//                                                                |
//          +-----------------+-----------------+-----------------+
//          |                 |                 |                 |
//       DynamicSlice(0)   DynamicSlice(1)   DynamicSlice(2)   DynamicSlice(3)
//          |                 |                 |                 |
//    w1 - MatMul       w1 - MatMul       w1 - MatMul       w1 - MatMul
//          |                 |                 |                 |
// Init- DynamicUpdate(0)- DynamicUpdate(1)- DynamicUpdate(2)- DynamicUpdate(3)
//                                                                |
//                                                               Loss

// Loop-based batch serialisation (batch serialization factor 4):
//
// Init     data [batch(4), c, h, w]
//  |        |
//  |     DynamicSlice(i)       }
//  |        |                  }
//  |  w0 - MatMul              } Loop(4)
//  |        |                  }
//  '-----DynamicUpdate(i)      }
//           |
// Init    BatchNorm
//  |        |
//  |     DynamicSlice(i)       }
//  |        |                  }
//  |  w1 - MatMul              } Loop(4)
//  |        |                  }
//  '-----DynamicUpdate(i)      }
//           |
//          Loss

namespace popart {
class Graph;

class BatchSerializedTensorInfo {
public:
  // Original tensor id before batch serialisation
  TensorId id;
  // Concatenated tensor id if the tensor has been concatenated after
  // serializing
  TensorId concatId;
  // Original tensor info before batch serialisation
  TensorInfo info;
  // Serialized tensor infos
  std::vector<TensorInfo> serializedInfos;
  // Serialized tensor ids
  std::vector<TensorId> serializedIds;
  // Type of shard tensor
  ShardTensorType type;
};

class BatchSerialize : public Transform {
public:
  static std::size_t id(int);

  BatchSerialize(int pass_) : Transform(), pass(pass_) {}
  virtual ~BatchSerialize() override {}

  virtual bool apply(Graph &graph) const override final;

  virtual std::size_t getId() const override final { return id(pass); }

  virtual std::string getName() const override final {
    return "BatchSerialize";
  }

private:
  OpId reshapeForSlice(Graph &graph,
                       Op::Settings settings,
                       TensorId inId,
                       Shape newShape,
                       TensorId newId,
                       OptionalBatchSerializedPhase bsp) const;

  int pass;
};

} // namespace popart

#endif
