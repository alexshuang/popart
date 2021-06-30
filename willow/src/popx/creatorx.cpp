// Copyright (c) 2019 Graphcore Ltd. All rights reserved.
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <memory>
#include <set>
#include <popart/popx/creatorx.hpp>
#include <popart/popx/devicex.hpp>
#include <popart/tensor.hpp>
#include <popart/tensorindex.hpp>

namespace popart {
namespace popx {

namespace {

std::vector<std::pair<view::Region, snap::Tensor>> fragment(
    const view::Region fullRegion,
    const std::vector<std::pair<view::Region, snap::Tensor>> tensorRegions) {

  std::vector<std::set<int64_t>> cuts(fullRegion.rank());

  for (auto &tr : tensorRegions) {
    for (int64_t i = 0; i < fullRegion.rank(); ++i) {
      cuts[i].insert(tr.first.getLower()[i]);
      cuts[i].insert(tr.first.getUpper()[i]);
    }
  }

  std::vector<std::pair<view::Region, snap::Tensor>> allTensorRegions;

  for (auto &tr : tensorRegions) {
    view::Regions cutRegions = tr.first.cut(cuts);
    for (auto &r : cutRegions) {
      allTensorRegions.push_back({r, tr.second});
    }
  }

  return allTensorRegions;
}

snap::Tensor
compose(const std::vector<std::pair<view::Region, snap::Tensor>> tensorRegions,
        const view::Region fullRegion,
        snap::Tensor fullTensor) {
  std::vector<std::pair<view::Region, snap::Tensor>> currentTensorRegions =
      tensorRegions;

  logging::devicex::trace("[creatorx] Full region {} {}",
                          fullRegion.getLower(),
                          fullRegion.getUpper());

  view::Regions regions;
  for (auto &tensorRegion : tensorRegions) {
    regions.push_back(tensorRegion.first);
    logging::devicex::trace("[creatorx] Tensor region {} {}",
                            regions.back().getLower(),
                            regions.back().getUpper());
  }

  // Regions for which a linear mapping is chosen
  view::Regions linearRegions = fullRegion.sub(regions);

  for (view::Region region : linearRegions) {
    logging::devicex::trace("[creatorx] Adding linear region {} {}",
                            region.getLower(),
                            region.getUpper());
    currentTensorRegions.emplace_back(region, fullTensor);
  }

  // At this point, currentTensorRegions should contain enough regions to
  // compose the tensor fully.

  // Fragment into concat-enable pieces
  currentTensorRegions = fragment(fullRegion, currentTensorRegions);

  // Cut pieces of the tensors that we want to keep for the final tensor
  for (int64_t i = 0; i < currentTensorRegions.size(); ++i) {
    auto region = currentTensorRegions[i].first;
    auto tensor = currentTensorRegions[i].second;
    // Tensor can either have the same shape as the region, or the full size
    // if it is the full size, cut down to relevant region size
    if (tensor.getPoplarTensor().numElements() > region.nelms()) {
      std::vector<size_t> l(region.rank());
      std::vector<size_t> u(region.rank());
      l.assign(region.getLower().begin(), region.getLower().end());
      u.assign(region.getUpper().begin(), region.getUpper().end());
      tensor = snap::Tensor{
          tensor.getPoplarTensor().slice(poplar::ArrayRef<size_t>(l),
                                         poplar::ArrayRef<size_t>(u)),
          tensor};
    }

    logging::trace("[creatorx] Tensor shape {} region {} {}",
                   tensor.getPoplarTensor().shape(),
                   region.getLower(),
                   region.getUpper());

    currentTensorRegions[i] = std::make_pair(region, tensor);
  }

  std::sort(currentTensorRegions.begin(),
            currentTensorRegions.end(),
            [](const std::pair<view::Region, snap::Tensor> &a,
               const std::pair<view::Region, snap::Tensor> &b) -> bool {
              return a.first.getLower() < b.first.getLower();
            });

  std::vector<std::pair<view::Region, snap::Tensor>> nextTensorRegions;

  // Merge along dimensions
  for (int64_t d = fullRegion.rank() - 1; d >= 0; --d) {
    nonstd::optional<view::Region> r;
    nonstd::optional<snap::Tensor> t;
    for (auto &tensorRegion : currentTensorRegions) {
      std::pair<int64_t, view::Region> rd(-1, view::Region({}, {}));
      if (r.has_value()) {
        rd = r.value().merge(tensorRegion.first);
      }
      if (rd.first != d) {
        // Can't merge regions directly
        if (r.has_value() && t.has_value()) {
          // Push back last region
          nextTensorRegions.push_back({r.value(), t.value()});
        }
        // Load next region
        r = tensorRegion.first;
        t = tensorRegion.second;
      } else {
        // Merge region & concatenate tensor if possible
        if (rd.first == d) {
          t = snap::Tensor{poplar::concat(t->getPoplarTensor(),
                                          tensorRegion.second.getPoplarTensor(),
                                          static_cast<uint32_t>(d)),
                           t.value()};
          r = rd.second;
        }
      }
    }
    if (t.has_value() && r.has_value()) {
      nextTensorRegions.push_back({r.value(), t.value()});
    }
    currentTensorRegions = nextTensorRegions;
    nextTensorRegions.clear();
  }

  return currentTensorRegions.front().second;
}

} // namespace

ICreatorCandidate::ICreatorCandidate() {}

bool ICreatorCandidate::greaterThan(ICreatorCandidatePtr icc1,
                                    ICreatorCandidatePtr icc2) {
  return std::tuple<double, int64_t, int64_t>(icc1->getMaxCreatorPriority(),
                                              icc1->getNumElems(),
                                              icc2->getScheduleIndex()) >
         std::tuple<double, int64_t, int64_t>(icc2->getMaxCreatorPriority(),
                                              icc2->getNumElems(),
                                              icc1->getScheduleIndex());
}

InputCreatorCandidate::InputCreatorCandidate(
    InIndex index_,
    const PopOpx *opx_,
    std::vector<OpxInAndOutIndex> pathFromInput_,
    int64_t scheduleIndex_)
    : index(index_), opx(opx_), scheduleIndex(scheduleIndex_) {

  pathFromInput.reserve(pathFromInput_.size());
  for (OpxInAndOutIndex &pathElem : pathFromInput_) {
    if (!pathElem.isDelegate) {
      pathFromInput.push_back(pathElem);
    }
  }
}

double InputCreatorCandidate::getMaxCreatorPriority() {
  return getOpx()->inputCreatorPriority;
}

int64_t InputCreatorCandidate::getNumElems() {
  int64_t n = 0;
  for (auto &r : unwind()) {
    n += r.nelms();
  }
  return n;
}

view::Regions InputCreatorCandidate::unwind() {
  return unwind(view::Region::getFull(opx->inShape(index)));
}

view::Regions InputCreatorCandidate::unwind(popart::view::Region region) {
  auto pathToInput = pathFromInput;
  std::reverse(pathToInput.begin(), pathToInput.end());
  view::Regions rqueue(1, region);
  view::Regions wqueue;
  for (auto &opxOnPath : pathToInput) {
    for (auto &r0 : rqueue) {
      auto regions = opxOnPath.opx->unwindRegion(opxOnPath.inIndex,
                                                 opxOnPath.outIndex)(r0);
      wqueue.insert(wqueue.end(), regions.begin(), regions.end());
    }
    rqueue = wqueue;
    wqueue.clear();
  }
  return rqueue;
}

std::pair<snap::Tensor, ViewChangers>
InputCreatorCandidate::unwind(snap::Tensor input) {

  // Reverse the path,
  // The first element is now the Opx producing a tensor consumed by
  // the candidate.
  // The last element is now the Opx consuming the input we are mapping.

  auto pathToInput = getPathsFromInput().front();
  std::reverse(pathToInput.begin(), pathToInput.end());

  auto region              = view::Region::getFull(opx->inShape(index));
  view::Regions outRegions = {region};
  view::Regions inRegions;

  for (auto &opxOnPath : pathToInput) {
    logging::devicex::trace("[creatorx] Unwinding at {}",
                            opxOnPath.opx->getOp<Op>().debugName());

    for (auto outRegion : outRegions) {
      auto rs = opxOnPath.opx->unwindRegion(opxOnPath.inIndex,
                                            opxOnPath.outIndex)(outRegion);
      for (auto &r : rs) {
        inRegions.push_back(r);
      }
    }

    auto expectedShape = opxOnPath.opx->getOp<Op>()
                             .output->tensor(opxOnPath.outIndex)
                             ->info.shape();
    auto fullRegion = view::Region::getFull(expectedShape);

    logging::devicex::trace("[creatorx] Expected shape {}", expectedShape);

    auto outInfo = opxOnPath.opx->getOp<Op>().outInfo(opxOnPath.outIndex);

    auto &graph = opxOnPath.opx->dstVirtualGraph(opxOnPath.outIndex);
    auto fullTensor =
        snap::Tensor{graph.getPoplarGraph().addVariable(
                         popType(outInfo), outInfo.shape_szt(), ""),
                     graph};

    // Map it linearly
    poputil::mapTensorLinearly(
        opxOnPath.opx->dstVirtualGraph(opxOnPath.outIndex).getPoplarGraph(),
        fullTensor.getPoplarTensor());

    logging::devicex::trace("[creatorx] Tensor shape before compose: {}",
                            input.getPoplarTensor().shape());

    std::vector<std::pair<view::Region, snap::Tensor>> tensorRegions;
    tensorRegions.reserve(outRegions.size());
    for (auto &tRegion : outRegions) {
      tensorRegions.push_back({tRegion, input});
    }

    // Compose a tensor of fullRegion shape, using as many of the tensorRegions
    // as necessary, and filling in missing pieces by taking them
    // from the linearly created fullTensor.
    input = compose(tensorRegions, fullRegion, fullTensor);

    logging::devicex::trace(
        "[creatorx] Tensor shape after compose / before unwind: {}",
        input.getPoplarTensor().shape());

    input = opxOnPath.opx->unwindTensorLayout(
        input, opxOnPath.inIndex, opxOnPath.outIndex);

    if (opxOnPath.opx->hasCreatorViewChangers(opxOnPath.inIndex)) {
      // Early stop unwinding; tensor has view change
      logging::devicex::debug(
          "[creatorx] Early stopping unwinding due to view-changing at Op {}",
          opxOnPath.opx->getOp<Op>().debugName());
      return {input, opxOnPath.opx->getCreatorViewChangers(opxOnPath.inIndex)};
    }

    logging::devicex::trace("[creatorx] Tensor shape after unwind: {}",
                            input.getPoplarTensor().shape());

    outRegions = inRegions;
    inRegions.clear();
  }

  if (pathToInput.size() > 0) {
    const Op &op              = pathToInput.back().opx->getOp<Op>();
    const Tensor *t           = op.input->tensor(pathToInput.back().inIndex);
    const Shape expectedShape = t->info.shape();
    auto fullRegion           = view::Region::getFull(expectedShape);

    logging::devicex::trace(
        "[creatorx] Expected final shape {} for tensor {} consumer {}",
        expectedShape,
        t->id,
        op.debugName());

    auto inInfo =
        pathToInput.back().opx->getOp<Op>().inInfo(pathToInput.back().inIndex);

    auto &graph =
        pathToInput.back().opx->srcVirtualGraph(pathToInput.back().inIndex);
    auto fullTensor = snap::Tensor{graph.getPoplarGraph().addVariable(
                                       popType(inInfo), inInfo.shape_szt(), ""),
                                   graph};

    // Map it linearly
    poputil::mapTensorLinearly(
        pathToInput.back()
            .opx->srcVirtualGraph(pathToInput.back().inIndex)
            .getPoplarGraph(),
        fullTensor.getPoplarTensor());

    logging::devicex::trace("[creatorx] Tensor shape before final compose: {}",
                            input.getPoplarTensor().shape());

    std::vector<std::pair<view::Region, snap::Tensor>> tensorRegions;
    tensorRegions.reserve(outRegions.size());
    for (auto &region_ : outRegions) {
      tensorRegions.push_back({region_, input});
    }

    // Compose a tensor of fullRegion shape, using as many of the tensorRegions
    // as necessary, and filling in missing pieces by taking them
    // from the linearly created fullTensor.
    input = compose(tensorRegions, fullRegion, fullTensor);

    logging::devicex::trace("[creatorx] Tensor shape after final compose: {}",
                            input.getPoplarTensor().shape());
  }

  return {input, ViewChangers()};
}

std::pair<snap::Tensor, ViewChangers>
InputCreatorCandidate::createInput(const poplar::DebugNameAndId &dnai) {
  snap::Tensor t = getOpx()->createInputTensor(getIndex(), dnai);
  if (getOpx()->hasCreatorViewChangers(getIndex())) {
    return {t, getOpx()->getCreatorViewChangers(getIndex())};
  }
  return unwind(t);
}

DnfTensorIds InputCreatorCandidate::mustExistBeforeCreate() {
  return getOpx()->mustExistBeforeCreateDNF(getIndex());
}

std::string InputCreatorCandidate::str() {

  std::string result = getOpx()->op_p->str();

  auto pathToInput = pathFromInput;
  std::reverse(pathToInput.begin(), pathToInput.end());

  result += "(";
  for (auto &i : pathToInput) {
    result += " -> " + i.opx->op_p->str() + " ";
    result += "[" + i.opx->op_p->output->id(i.outIndex);
    result += "->" + i.opx->op_p->input->id(i.inIndex) + "]";
  }
  result += ")";

  return result;
}

InputMultiCreatorCandidate::InputMultiCreatorCandidate()
    : ICreatorCandidate() {}

double InputMultiCreatorCandidate::getMaxCreatorPriority() {
  double priority = std::numeric_limits<double>::lowest();
  for (auto &candidate : candidates) {
    priority = std::max(candidate.first->getMaxCreatorPriority(), priority);
  }
  return priority;
}

view::Regions InputMultiCreatorCandidate::unwind() {
  throw("Not expected to unwind on InputMultiCreatorCandidate");
}

view::Regions InputMultiCreatorCandidate::unwind(popart::view::Region) {
  throw("Not expected to unwind on InputMultiCreatorCandidate");
}

std::pair<snap::Tensor, ViewChangers>
InputMultiCreatorCandidate::unwind(snap::Tensor) {
  throw("Not expected to unwind on InputMultiCreatorCandidate");
}

DnfTensorIds InputMultiCreatorCandidate::mustExistBeforeCreate() {
  DnfTensorIds cumulativeTensorIds;
  for (auto &candidate : candidates) {
    if (!candidate.first->mustExistBeforeCreate().empty()) {
      if (cumulativeTensorIds.empty()) {
        cumulativeTensorIds = candidate.first->mustExistBeforeCreate();
      } else {
        // Distribute DNF over DNF
        DnfTensorIds newCumulativeTensorIds;
        for (auto tensorIds0 : cumulativeTensorIds) {
          for (auto tensorIds1 : candidate.first->mustExistBeforeCreate()) {
            std::set<TensorId> tensorIds2 = tensorIds0;
            tensorIds2.insert(tensorIds1.begin(), tensorIds1.end());
            newCumulativeTensorIds.push_back(tensorIds2);
          }
        }
        cumulativeTensorIds = newCumulativeTensorIds;
      }
    }
  }
  return cumulativeTensorIds;
}

int64_t InputMultiCreatorCandidate::getScheduleIndex() const {
  int64_t index = 0;
  for (auto &candidate : candidates) {
    index = std::max(index, candidate.first->getScheduleIndex());
  }
  return index;
}

int64_t InputMultiCreatorCandidate::getNumElems() {
  int64_t elems = 0;
  // Loop over all candidates
  for (auto &candidate : candidates) {
    // Loop over all accepted regions
    for (auto region : candidate.second) {
      elems += region.nelms();
    }
  }
  return elems;
}

// Create tensor by composing parts created by candidates
std::pair<snap::Tensor, ViewChangers>
InputMultiCreatorCandidate::createInput(const poplar::DebugNameAndId &dnai) {
  auto candidateIdx = 0;

  std::vector<std::pair<view::Region, snap::Tensor>> currentTensorRegions;

  for (auto &candidate : candidates) {
    auto tensorAndView = candidate.first->createInput(
        {dnai, logging::format("fragment/{}", std::to_string(candidateIdx))});
    auto tensor = tensorAndView.first;
    logging::devicex::trace("Accepted candidate regions: {}, tensor shape: {}",
                            candidate.second,
                            tensor.getPoplarTensor().shape());
    for (auto acceptedRegion : candidate.second)
      currentTensorRegions.push_back({acceptedRegion, tensor});
    ++candidateIdx;
  }

  // Fallback linearly mapped tensor, inferred from first candidate
  auto popShape = currentTensorRegions.front().second.getPoplarTensor().shape();
  std::vector<int64_t> shape(popShape.size());
  shape.assign(popShape.begin(), popShape.end());
  auto fullRegion = view::Region::getFull(shape);
  auto fullTensor = currentTensorRegions.front().second;

  return {compose(currentTensorRegions, fullRegion, fullTensor),
          ViewChangers()};
}

std::string InputMultiCreatorCandidate::str() {
  std::stringstream ss;
  ss << "[" << std::endl;
  for (auto candidate : candidates) {
    ss << candidate.first->str() << std::endl;
  }
  ss << "]";
  return ss.str();
}

view::Regions
InputMultiCreatorCandidate::getAcceptedSubregions(view::Region other) {
  view::Regions remainder = {};
  view::Regions stack     = {other};
  for (auto &candidate : candidates) {
    // Loop over accepted regions for each creator
    for (auto region : candidate.second) {
      while (stack.size() > 0) {
        view::Region r0 = stack.back();
        stack.pop_back();
        view::Regions tmp = r0.sub(region);
        remainder.insert(remainder.end(), tmp.begin(), tmp.end());
      }
      stack = remainder;
      remainder.clear();
    }
  }
  return stack;
}

bool InputMultiCreatorCandidate::addCreatorCandidate(
    ICreatorCandidatePtr candidate) {
  view::Regions acceptedRegions;
  view::Regions regions = candidate->unwind();
  for (auto &r : regions) {
    auto acceptedSubregions = getAcceptedSubregions(r);
    acceptedRegions.insert(acceptedRegions.end(),
                           acceptedSubregions.begin(),
                           acceptedSubregions.end());
  }
  if (acceptedRegions.size() > 0) {
    candidates.insert({candidate, acceptedRegions});
    return true;
  }
  return false;
}

std::vector<std::vector<OpxInAndOutIndex>>
InputMultiCreatorCandidate::getPathsFromInput() {
  std::vector<std::vector<OpxInAndOutIndex>> paths;
  for (auto &candidate : candidates) {
    for (auto &path : candidate.first->getPathsFromInput()) {
      paths.push_back(path);
    }
  }
  return paths;
}

} // namespace popx
} // namespace popart
