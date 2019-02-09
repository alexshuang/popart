echo "Inplace clang-formatting all .cpp files in listed directories,"
clang-format -i python/poponnx.cpp
clang-format -i willow/src/*cpp
clang-format -i willow/src/popx/*cpp
clang-format -i willow/src/popx/op/*cpp
clang-format -i willow/src/patterns/*cpp
clang-format -i willow/src/op/*cpp
clang-format -i willow/src/ces/*cpp
clang-format -i willow/src/transforms/*cpp

echo "inplace clang-formatting all .hpp files in listed directories,"
clang-format -i willow/include/poponnx/*hpp
clang-format -i willow/include/poponnx/popx/*hpp
clang-format -i willow/include/poponnx/popx/op/*hpp
clang-format -i willow/include/poponnx/patterns/*hpp
clang-format -i willow/include/poponnx/op/*hpp
clang-format -i willow/include/poponnx/ces/*hpp
clang-format -i willow/include/poponnx/transforms/*hpp
clang-format -i willow/examples/cplusplus/*cpp
clang-format -i tests/poponnx/*cpp
clang-format -i tests/poponnx/constexpr_tests/*cpp
clang-format -i tests/poponnx/inplace_tests/*cpp
clang-format -i tests/poponnx/dot_tests/*cpp

echo "inplace yapfing all .py files in listed directories,"
python3 -m yapf -i tests/torch/cifar10/*py
python3 -m yapf -i tests/poponnx/*py
python3 -m yapf -i scripts/*py
python3 -m yapf -i tests/poponnx/operators_test/*py
python3 -m yapf -i python/poponnx/torch/*py
python3 -m yapf -i python/poponnx/*py

echo "formatting complete."
