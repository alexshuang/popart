#!/bin/sh

set -ex

# $1: metagraph.txt $2: metagraph

grep 'Recv:' $1 | awk -F' ' '{print $2}' > inputs.txt
sed -i 's/^"\(.*\)"$/\1,/g' inputs.txt
sort inputs.txt | uniq > /tmp/inputs.txt
cp /tmp/inputs.txt inputs.txt
sed -i ':a;N;$!ba;s/\n//g' inputs.txt
sed -i 's/,$//g' inputs.txt

grep -r 'score\>' $1 | grep -v 'input: ' | awk -F' ' '{print $2}' > outputs.txt
sed -i 's/^"\(.*\)"$/\1,/g' outputs.txt
sort outputs.txt | uniq > /tmp/outputs.txt
cp /tmp/outputs.txt outputs.txt
sed -i ':a;N;$!ba;s/\n//g' outputs.txt
sed -i 's/,$//g' outputs.txt

INPUTS=`cat inputs.txt`
OUTPUTS=`cat outputs.txt`

python -m tf2onnx.convert --checkpoint $2 --output model.onnx --inputs $INPUTS --outputs $OUTPUTS --opset 11 --custom-ops TensorswitchStreamRecvInit,TensorswitchStreamRecv
