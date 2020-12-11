@0x81c65475b387e153; # unique file ID, generated by `capnp id`

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("popart::popx::cap");

using Ir = import "Ir.capnp";

struct TensorInterval{
  start @0: UInt64;
  end @1: UInt64;
}

using TensorIntervalList = List(TensorInterval);
using TensorId = Text;

struct IrLowering {
  struct TensorTileMap{
    mappings @0: List(Mapping);
    struct Mapping{
      id @0 :Text;
      tensorIntervalLists @1 :List(TensorIntervalList);
    }
  }

  ir @0: Ir.Ir;
  linearlyCreatedInputTensors @1: List(TensorId);
  efficientlyCreatedInputTensors @2: List(TensorId);
  hostReduceStreamIds @3: List(TensorId);
  cycleCountIds @4: List(TensorId);
}