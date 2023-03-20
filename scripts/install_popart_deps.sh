#!/bin/sh

CPUCORES=64

mkdir -p deps

set -ex

export SPDLOG_INSTALL_DIR=$(pwd)/deps/spdlog-1.8.0/install_dir/
export PYBIND11_INSTALL_DIR=$(pwd)/deps/pybind11-2.6.2/install_dir/
export BOOST_INSTALL_DIR=$(pwd)/deps/boost_1_80_0/install_dir/
export ONNX_INSTALL_DIR=$(pwd)/deps/onnx-1.6.0/install_dir/
export CAPNPROTO_INSTALL_DIR=$(pwd)/deps/capnproto-0.7.0/install_dir/
export TROMPELOEIL_INSTALL_DIR=$(pwd)/deps/trompeloeil-35/install_dir/
export POPRITHMS_INSTALL_DIR=$(pwd)/deps/poprithms/install_dir/
export PROTOBUF_INSTALL_DIR=$(pwd)/deps/protobuf-3.6.1/install_dir
export POPART_INSTALL_DIR=$(pwd)/install_dir/
export PKG_CONFIG_PATH="$CAPNPROTO_INSTALL_DIR/lib/pkgconfig:$PKG_CONFIG_PATH"

pushd `mktemp -d`
wget https://github.com/protocolbuffers/protobuf/releases/download/v3.6.1/protobuf-cpp-3.6.1.tar.gz
tar xvfz protobuf-cpp-3.6.1.tar.gz
rm protobuf-cpp-3.6.1.tar.gz
cd protobuf-3.6.1
CXXFLAGS=-fPIC CFLAGS=-fPIC ./configure --prefix=$PROTOBUF_INSTALL_DIR
make -j8
make install
popd

pushd `mktemp -d`
git clone --branch v1.8.0 https://github.com/gabime/spdlog.git
cd spdlog && mkdir build && cd build
cmake .. -GNinja -DCMAKE_INSTALL_PREFIX=$SPDLOG_INSTALL_DIR && cmake --build . --target install
popd

pushd `mktemp -d`
wget https://github.com/pybind/pybind11/archive/v2.6.2.tar.gz
tar xvfz v2.6.2.tar.gz
rm v2.6.2.tar.gz
cd pybind11-2.6.2
mkdir build
mkdir install_dir
cd build
cmake .. \
  -DCMAKE_INSTALL_PREFIX=$PYBIND11_INSTALL_DIR \
  -GNinja
ninja
ninja install
popd

pushd `mktemp -d`
wget https://boostorg.jfrog.io/artifactory/main/release/1.80.0/source/boost_1_80_0.tar.bz2
tar xf boost_1_80_0.tar.bz2
rm boost_1_80_0.tar.bz2
cd boost_1_80_0
./bootstrap.sh --prefix=$BOOST_INSTALL_DIR
./b2 -j$CPUCORES link=static runtime-link=static --abbreviate-paths variant=release toolset=gcc "cxxflags= -fno-semantic-interposition -fPIC" cxxstd=14 --with-test --with-system --with-filesystem --with-program_options --with-graph --with-random install
popd

pushd `mktemp -d`
wget https://github.com/onnx/onnx/archive/v1.6.0.tar.gz
tar xvfz v1.6.0.tar.gz
rm v1.6.0.tar.gz
cd onnx-1.6.0
mkdir build
cd build
cmake .. \
  -DONNX_ML=0 \
  -DProtobuf_PROTOC_EXECUTABLE=$PROTOBUF_INSTALL_DIR/bin/protoc \
  -DCMAKE_INSTALL_PREFIX=$ONNX_INSTALL_DIR
make -j$CPUCORES
make install
popd

pushd `mktemp -d`
wget https://capnproto.org/capnproto-c++-0.7.0.tar.gz
tar xvfz capnproto-c++-0.7.0.tar.gz
rm capnproto-c++-0.7.0.tar.gz
cd capnproto-c++-0.7.0
./configure --prefix=$CAPNPROTO_INSTALL_DIR
make -j$CPUCORES check
make install
popd

pushd `mktemp -d`
wget https://github.com/rollbear/trompeloeil/archive/refs/tags/v35.tar.gz
tar xvfz v35.tar.gz
rm v35.tar.gz
cd trompeloeil-35
mkdir build ; cd build
cmake .. -DCMAKE_INSTALL_PREFIX=$TROMPELOEIL_INSTALL_DIR
cmake --build . --target install
popd

#pushd `mktemp -d`
#git clone https://github.com/graphcore/poprithms.git
#cd poprithms
#mkdir build; cd build;
#cmake .. \
#  -DBOOST_ROOT=$BOOST_INSTALL_DIR \
#  -DCMAKE_INSTALL_PREFIX=$POPRITHMS_INSTALL_DIR \
#  -DCMAKE_GENERATOR="Ninja"
#ninja
#ninja install
#popd

##git clone https://github.com/graphcore/popart.git
#git clone ssh://git@phabricator.sourcevertex.net/diffusion/POPONNX/poponnx-v2.git
#cd poponnx-v2
#mkdir build; cd build;
#cmake .. \
#  -DBOOST_ROOT=$BOOST_INSTALL_DIR \
#  -DCapnProto_ROOT=$CAPNPROTO_INSTALL_DIR \
#  -DONNX_ROOT=$ONNX_INSTALL_DIR \
#  -DPOPLAR_INSTALL_DIR=$POPLAR_INSTALL_DIR \
#  -Dpoprithms_ROOT=$POPRITHMS_INSTALL_DIR \
#  -DProtobuf_ROOT=$PROTOBUF_INSTALL_DIR \
#  -Dpybind11_ROOT=$PYBIND11_INSTALL_DIR \
#  -Dspdlog_ROOT=$SPDLOG_INSTALL_DIR \
#  -Dtrompeloeil_ROOT=$TROMPELOEIL_INSTALL_DIR \
#  -DCMAKE_INSTALL_PREFIX=$POPART_INSTALL_DIR \
#  -GNinja
#ninja
#ninja install
#popd

echo "export PROTOBUF_INSTALL_DIR=${PROTOBUF_INSTALL_DIR}" >> ~/.bashrc
echo "export SPDLOG_INSTALL_DIR=${SPDLOG_INSTALL_DIR}" >> ~/.bashrc
echo "export PYBIND11_INSTALL_DIR=${PYBIND11_INSTALL_DIR}" >> ~/.bashrc
echo "export BOOST_INSTALL_DIR=${BOOST_INSTALL_DIR}" >> ~/.bashrc
echo "export ONNX_INSTALL_DIR=${ONNX_INSTALL_DIR}" >> ~/.bashrc
echo "export CAPNPROTO_INSTALL_DIR=${CAPNPROTO_INSTALL_DIR}" >> ~/.bashrc
echo "export TROMPELOEIL_INSTALL_DIR=${TROMPELOEIL_INSTALL_DIR}" >> ~/.bashrc
echo "export POPRITHMS_INSTALL_DIR=${POPRITHMS_INSTALL_DIR}" >> ~/.bashrc
source ~/.bashrc
