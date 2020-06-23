// Copyright (c) 2020 Graphcore Ltd. All rights reserved.
#ifndef GUARD_NEURALNET_STEPIOSPLITTER_HPP
#define GUARD_NEURALNET_STEPIOSPLITTER_HPP

#include <popart/istepio.hpp>

#include <list>
#include <map>
#include <tuple>

namespace popart {

// Forward declaration.
class StepIOSplitter;

// A helper class that acts as a downstream interface for input and output data
// streams.
class StepIOSplitterAdapter : public IStepIO {
public:
  // Constructor.
  StepIOSplitterAdapter(StepIOSplitter *splitter,
                        unsigned replicationIndex,
                        TensorId id);
  // Destructor.
  virtual ~StepIOSplitterAdapter() = default;
  // Get next data element for reading from adapter.
  virtual ConstVoidData in(TensorId id, int64_t numElements, bool prefetch);
  // Move on to next data element.
  virtual void inComplete(TensorId id, int64_t numElements);
  // Get next data element for writing from adapter.
  virtual MutableVoidData out(TensorId id, int64_t numElements);
  // Move on to next data element.
  virtual void outComplete(TensorId);
  // Check number of elements.
  virtual void assertNumElements(const Ir &ir) const;

  // Reset all in/out data.
  void reset();

  // Get reference to in data buffer.
  std::list<ConstVoidData> &getInData() { return inData; }
  // Get reference to out data buffer.
  std::list<MutableVoidData> &getOutData() { return outData; }

private:
  // Reference back to StepIOSplitter object.
  StepIOSplitter *splitter;
  // Replication index.
  unsigned replicationIndex;
  // The id this adapter was created for.
  TensorId adapterId;
  // Buffer of elements to read from.
  std::list<ConstVoidData> inData;
  // Buffer of elements to write into.
  std::list<MutableVoidData> outData;
};

// A class that splits one StepIO interface into multiple StepIO interfaces that
// can be read/written to by multiple replicas separately.
class StepIOSplitter {
public:
  // Constructor.
  StepIOSplitter(unsigned replicationFactor);
  // Don't allow copying.
  StepIOSplitter(const StepIOSplitter &) = delete;
  // Don't allow assigning.
  StepIOSplitter &operator=(const StepIOSplitter &) = delete;
  // Destructor.
  virtual ~StepIOSplitter() = default;

  // Reset the logic.
  void reset();
  // Reset the log and set the upstream IStepIO.
  void reset(IStepIO *upstreamIo);

  // Fetch in data from upstream.
  void getInData(TensorId id, int64_t numElements);
  // Fetch out data from upstream.
  void getOutData(TensorId id, int64_t numElements);

  // Check number of elements in upstream IStepIO.
  virtual void assertNumElements(const Ir &) const;

  // Get access to the 'split' data stream.
  IStepIO *getDownstreamStepIO(TensorId id, unsigned replicationIndex);

private:
  // The number of replications.
  unsigned replicationFactor;

  // The upstream datastream.
  IStepIO *upstreamIo;
  // Map tuples TensorId to a map from replication indices to IStepIO adapters.
  std::map<TensorId, std::map<unsigned, std::unique_ptr<StepIOSplitterAdapter>>>
      downstreamIoMap;
};

} // namespace popart

#endif