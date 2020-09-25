// Copyright (c) 2018 Graphcore Ltd. All rights reserved.
#include <algorithm>
#include <chrono>
#include <random>
#include <sstream>
#include <thread>

#include <poplar/DeviceManager.hpp>
#include <poplar/OptionFlags.hpp>
#include <popart/devicemanager.hpp>
#include <popart/error.hpp>

namespace popart {

SyncPattern syncPatternFromString(const std::string &str) {
  if (str == "full") {
    return SyncPattern::Full;
  }
  if (str == "singlePipeline") {
    return SyncPattern::SinglePipeline;
  }
  if (str == "replicaAndLadder") {
    return SyncPattern::ReplicaAndLadder;
  }

  throw error("Unknown syncPattern setting: {}", str);
}

std::string syncPatternToString(SyncPattern pattern) {
  switch (pattern) {
  case SyncPattern::Full:
    return "full";
  case SyncPattern::SinglePipeline:
    return "singlePipeline";
  case SyncPattern::ReplicaAndLadder:
    return "replicaAndLadder";
  }
  throw error("Unknown syncPattern setting: {}", static_cast<int>(pattern));
}

DeviceManager &DeviceManager::createDeviceManager() {
  static DeviceManager deviceManager;
  return deviceManager;
}

void DeviceManager::registerDeviceProvider(DeviceProvider *provider) {
  providers.push_back(provider);
}

std::shared_ptr<DeviceInfo>
DeviceManager::getDevice(SyncPattern syncPattern,
                         unsigned deviceManagerId,
                         DeviceConnectionType connectionType) {
  if (connectionType == DeviceConnectionType::Never) {
    throw error("Trying to acquire a hardware device when connectionType is "
                "DeviceConnectionType::Never. For offline compilation, use "
                "createOfflineIPUDevice");
  }
  for (auto p : providers) {
    auto device = p->getDevice(syncPattern, deviceManagerId, connectionType);
    if (device != nullptr) {
      return device;
    }
  }
  return nullptr;
}

std::vector<std::shared_ptr<DeviceInfo>>
DeviceManager::enumerateDevices(SyncPattern pattern,
                                int numIpus,
                                DeviceType deviceType,
                                DeviceConnectionType connectionType) {
  std::vector<std::shared_ptr<DeviceInfo>> devices;

  for (auto p : providers) {
    p->enumerate(devices, numIpus, pattern, deviceType, connectionType);
  }
  for (auto d : devices) {
    logging::debug("Device: {}", d.get()->toString());
  }

  for (auto device : devices) {
    device->setOnDemandAttachTimeout(attachTimeout);
  }

  return devices;
}

std::shared_ptr<DeviceInfo> DeviceManager::createCpuDevice() {
  for (auto p : providers) {
    std::shared_ptr<DeviceInfo> device =
        p->createHostDevice(DeviceType::Cpu, {});
    if (device != nullptr)
      return device;
  }
  return nullptr;
}

std::shared_ptr<DeviceInfo> DeviceManager::createIpuModelDevice(
    std::map<std::string, std::string> &options) {
  for (auto p : providers) {
    std::shared_ptr<DeviceInfo> device =
        p->createHostDevice(DeviceType::IpuModel, options);
    if (device != nullptr)
      return device;
  }
  return nullptr;
}

std::shared_ptr<DeviceInfo>
DeviceManager::createSimDevice(std::map<std::string, std::string> &options) {
  for (auto p : providers) {
    std::shared_ptr<DeviceInfo> device =
        p->createHostDevice(DeviceType::Sim, options);
    if (device != nullptr)
      return device;
  }
  return nullptr;
}

std::shared_ptr<DeviceInfo> DeviceManager::createOfflineIPUDevice(
    std::map<std::string, std::string> &options) {

  for (auto p : providers) {
    std::shared_ptr<DeviceInfo> device =
        p->createHostDevice(DeviceType::OfflineIpu, options);
    if (device != nullptr) {
      return device;
    }
  }
  return nullptr;
}

std::shared_ptr<DeviceInfo> DeviceManager::acquireAvailableDevice(
    int numIpus,
    int tilesPerIPU,
    SyncPattern pattern,
    DeviceConnectionType connectionType,
    DeviceSelectionCriterion selectionCriterion) {
  if (numIpus > 0 && ((numIpus & (numIpus - 1)) != 0)) {
    throw error("You have attempted to acquire {} IPUs. The number of IPUs "
                "requested must be a power of two",
                numIpus);
  }
  if (connectionType == DeviceConnectionType::Never) {
    throw error("Trying to acquire a hardware device when connectionType is "
                "DeviceConnectionType::Never");
  }

  auto devices =
      enumerateDevices(pattern, numIpus, DeviceType::Ipu, connectionType);

  std::mt19937 g(/* seed */ 1);

  if (selectionCriterion == DeviceSelectionCriterion::Random) {
    std::shuffle(devices.begin(), devices.end(), g);
  }

  for (auto &device : devices) {
    if ((!tilesPerIPU || tilesPerIPU == device->getTilesPerIPU())) {
      // Attach to the device. Will succeed if available
      if (connectionType == DeviceConnectionType::Always) {
        if (device->attach()) {
          return device;
        }
      } else {
        return device;
      }
    }
  }

  return nullptr;
}

std::shared_ptr<DeviceInfo>
DeviceManager::acquireDeviceById(int id,
                                 SyncPattern pattern,
                                 DeviceConnectionType connectionType) {
  if (connectionType == DeviceConnectionType::Never) {
    throw error("Trying to acquire a hardware device when connectionType is "
                "DeviceConnectionType::Never");
  }

  auto device = getDevice(pattern, id, connectionType);

  // Attach to the device. Will succeed if available
  if (connectionType == DeviceConnectionType::Always) {
    if (device->attach()) {
      return device;
    } else {
      return nullptr;
    }
  }

  return device;
}

void DeviceManager::setOnDemandAttachTimeout(const unsigned seconds) {
  attachTimeout = seconds;
}

std::ostream &operator<<(std::ostream &os, const DeviceType &dt) {
  switch (dt) {
  case DeviceType::Cpu:
    os << "cpu";
    break;
  case DeviceType::Ipu:
    os << "ipu";
    break;
  case DeviceType::IpuModel:
    os << "ipu-model";
    break;
  case DeviceType::OfflineIpu:
    os << "offline-ipu";
    break;
  case DeviceType::Sim:
    os << "sim";
    break;
  }

  return os;
}

std::ostream &operator<<(std::ostream &os, const DeviceConnectionType &dct) {
  switch (dct) {
  case DeviceConnectionType::Always:
    os << "always";
    break;
  case DeviceConnectionType::OnDemand:
    os << "on-demand";
    break;
  case DeviceConnectionType::Never:
    os << "never";
    break;
  }

  return os;
}

std::string DeviceInfo::toString() const {
  std::stringstream ss;

  ss << "Device Type:" << getType()
     << " Connection Type:" << getConnectionType() << " Id:" << getId()
     << " Version:" << getVersion() << " NumIPUs:" << getNumIpus()
     << " NumTilesPerIPU:" << getTilesPerIPU();

  ss << " DeviceIds: {";
  std::string sep;
  for (unsigned i : getDriverIds()) {
    ss << sep << i;
    sep = ",";
  }
  ss << "}";

  // TODO : Add all the information from Target

  return ss.str();
}

void DeviceInfo::setOnDemandAttachTimeout(const unsigned seconds) {
  attachTimeout = seconds;
}

bool DeviceInfo::tryAttachUntilTimeout() {
  // Periodically try to attach until either timeout reached or
  // successfully attached
  auto startTime = std::chrono::steady_clock::now();
  unsigned wait  = 0;
  bool attached  = false;
  while (wait < getOnDemandAttachTimeout() && !attached) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    attached       = attach();
    auto delayTime = std::chrono::steady_clock::now();
    wait =
        std::chrono::duration_cast<std::chrono::seconds>(delayTime - startTime)
            .count();
  }
  return attached;
}

std::ostream &operator<<(std::ostream &os, const DeviceInfo &di) {
  return os << di.toString();
}

} // namespace popart
