// Copyright (c) 2021 Graphcore Ltd. All rights reserved.
#include <transforms/autodiff/autodiffhelper.hpp>

namespace popart {

AutodiffHelper::AutodiffHelper(AutodiffIrInterface &dep_) : dep(dep_) {}

} // namespace popart