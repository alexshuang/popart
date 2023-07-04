#!/bin/sh

# Install prerequisites.
pushd `mktemp -d`
git clone ssh://git@phabricator.sourcevertex.net/diffusion/POPONNX/poponnx-v2.git popart
sed -i 's/\(typing-extensions>=3.*\)/# \1/g' popart/requirements/dev.txt
pip install -r popart/requirements/dev.txt
rm -rf popart
popd

# Build PopART
git clone ssh://git@phabricator.sourcevertex.net/diffusion/POPARTVIEW/popart_view.git
cd popart_view
gc-view pull

# rm unused cmd
line_number=`grep -n 'should not be root' scripts/build.sh | cut -d ':' -f 1`
if [ -n "$line_number" ]; then
  start_line=$((line_number - 1))
  end_line=$((line_number + 2))
  sed -i "$start_line,${end_line}d" scripts/build.sh
fi

# sdk 3.4 is base on 20.04
sed -i "s;\(unpacked/poplar/poplar-ubuntu\)_18_04;\1_20_04;g" scripts/build.sh

chmod 755 scripts/build.sh
./scripts/build.sh
