#!/bin/bash

# stop on error
set -e

# Install toolings to compile dependencies
sudo apt install autoconf automake libtool curl make g++ unzip libreadline-dev libgtest-dev pkg-config || true

INSTALL_PREFIX=$PWD/.dep
DOWNLOAD_DIR=$INSTALL_PREFIX/download
mkdir -p $INSTALL_PREFIX
cd $INSTALL_PREFIX

if [ -n "$(find $INSTALL_PREFIX -name 'libzmq.a')" ]; then
  echo "Found zmq. Skipping installation."
else
  mkdir -p $DOWNLOAD_DIR
  cd $DOWNLOAD_DIR

  echo "Downloading zmq"
  wget -nc https://github.com/zeromq/libzmq/releases/download/v4.3.2/zeromq-4.3.2.tar.gz 
  tar -xzf zeromq-4.3.2.tar.gz
  rm -f zeromq-4.3.2.tar.gz

  echo "Installing zmq"
  cd zeromq-4.3.2
  mkdir -p build
  cd build
  cmake .. -D WITH_PERF_TOOL=OFF -D ZMQ_BUILD_TESTS=OFF -D CMAKE_INSTALL_PREFIX:PATH=$INSTALL_PREFIX
  make -j$(nproc) install
  cd ../..

  cd ..
  rm -rf $DOWNLOAD_DIR
fi

if [ -n "$(find $INSTALL_PREFIX -name 'cppzmq*')" ]; then
  echo "Found cppzmq. Skipping installation."
else
  mkdir -p $DOWNLOAD_DIR
  cd $DOWNLOAD_DIR
  echo "Downloading cppzmq"
  wget -nc https://github.com/zeromq/cppzmq/archive/v4.5.0.tar.gz
  tar -xzf v4.5.0.tar.gz
  rm -rf v4.5.0.tar.gz

  echo "Installing cppzmq"
  cd cppzmq-4.5.0
  mkdir -p build
  cd build
  cmake .. -D CMAKE_INSTALL_PREFIX:PATH=$INSTALL_PREFIX
  make -j$(nproc) install
  cd ../..

  cd ..
  rm -rf $DOWNLOAD_DIR
fi

if [ -n "$(find $INSTALL_PREFIX -name 'libprotobuf*')" ]; then
  echo "Found protobuf. Skipping installation."
else
  mkdir -p $DOWNLOAD_DIR
  cd $DOWNLOAD_DIR
  echo "Downloading protobuf"
  wget -nc https://github.com/protocolbuffers/protobuf/releases/download/v3.11.1/protobuf-cpp-3.11.1.tar.gz
  tar -xzf protobuf-cpp-3.11.1.tar.gz
  rm -rf protobuf-cpp-3.11.1.tar.gz

  echo "Installing protobuf"
  cd protobuf-3.11.1
  ./autogen.sh
  ./configure --prefix=$INSTALL_PREFIX
  make -j$(nproc) install
  cd ..

  cd ..
  rm -rf $DOWNLOAD_DIR
fi 
