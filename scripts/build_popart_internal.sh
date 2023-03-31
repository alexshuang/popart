#!/bin/sh

CPUCORES=64

git clone ssh://git@phabricator.sourcevertex.net/diffusion/POPONNX/poponnx-v2.git
pushd poponnx-v2

export PKG_CONFIG_PATH="$CAPNPROTO_INSTALL_DIR/lib/pkgconfig:$PKG_CONFIG_PATH"

#git clone https://github.com/graphcore/popart.git
#pushd popart
mkdir build
cd build

set -ex

cmake .. \
  -DBOOST_ROOT=$BOOST_INSTALL_DIR \
  -DCapnProto_ROOT=$CAPNPROTO_INSTALL_DIR \
  -DONNX_ROOT=$ONNX_INSTALL_DIR \
  -DPOPLAR_INSTALL_DIR=$POPLAR_INSTALL_DIR \
  -Dpoprithms_ROOT=$POPRITHMS_INSTALL_DIR \
  -Dpybind11_ROOT=$PYBIND11_INSTALL_DIR \
  -Dspdlog_ROOT=$SPDLOG_INSTALL_DIR \
  -Dtrompeloeil_ROOT=$TROMPELOEIL_INSTALL_DIR \
  -DCMAKE_INSTALL_PREFIX=$POPART_INSTALL_DIR \

  -GNinja
ninja
ninja install
popd
