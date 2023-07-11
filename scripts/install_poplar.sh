#!/bin/sh

set -ex

SRC=$1
DIR=${1%/*}
if [ x"$DIR" == x"$SRC" ]; then
  DIR=./
fi
FILE=${1##*/}
BASENAME=${FILE%.tar.gz}
DIR=$DIR/$BASENAME

if [ -z "$BASENAME" ]; then
    echo "invalid basename: $BASENAME"
    exit 1
fi

rm -rf $DIR
mkdir -p $DIR
tar xf $SRC -C $DIR
cd $DIR/poplar*/poplar*

rm -rf /opt/poplar
ln -s $PWD /opt/poplar

echo -e "\nexport POPLAR_INSTALL_DIR=$PWD\n" >> ~/.bashrc
echo "unset POPLAR_SDK_ENABLED" >> ~/.bashrc
echo "source \$POPLAR_INSTALL_DIR/enable.sh" >> ~/.bashrc

cd $DIR/poplar*/popart*
echo -e "\nexport POPART_INSTALL_DIR=$PWD\n" >> ~/.bashrc
echo "source \$POPART_INSTALL_DIR/enable.sh" >> ~/.bashrc

echo "done"
echo "You need to source ~/.bashrc to enable poplar SDK."
