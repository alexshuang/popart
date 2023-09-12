#!/bin/sh

SRC_DIR=${1:-$PWD}

mkdir -p $SRC_DIR/build && cd -
cmake .. -DBOOST_ROOT=$BOOST_INSTALL_DIR  -Dspdlog_ROOT=$SPDLOG_INSTALL_DIR -Dpybind11_ROOT=$PYBIND11_INSTALL_DIR
make -j30 install
