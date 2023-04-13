#!/bin/sh

set -x

export POPART_INSTALL_DIR=$(pwd)/popart/install_dir/
export PKG_CONFIG_PATH="$CAPNPROTO_INSTALL_DIR/lib/pkgconfig:$PKG_CONFIG_PATH"
git clone https://github.com/graphcore/popart.git
cd popart
mkdir build; cd build;
cmake .. \
  -DBOOST_ROOT=$BOOST_INSTALL_DIR \
  -DCapnProto_ROOT=$CAPNPROTO_INSTALL_DIR \
  -DONNX_ROOT=$ONNX_INSTALL_DIR \
  -DPOPLAR_INSTALL_DIR=$POPLAR_INSTALL_DIR \
  -Dpoprithms_ROOT=$POPRITHMS_INSTALL_DIR \
  -DProtobuf_ROOT=$PROTOBUF_INSTALL_DIR \
  -Dpybind11_ROOT=$PYBIND11_INSTALL_DIR \
  -Dspdlog_ROOT=$SPDLOG_INSTALL_DIR \
  -Dtrompeloeil_ROOT=$TROMPELOEIL_INSTALL_DIR \
  -DCMAKE_INSTALL_PREFIX=$POPART_INSTALL_DIR \
  -GNinja
ninja -j$(nproc)
ninja install
