// Copyright 2015 The Weave Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/privet/device_delegate.h"

#include <base/guid.h>

#include "src/privet/constants.h"

namespace weave {
namespace privet {

namespace {

class DeviceDelegateImpl : public DeviceDelegate {
 public:
  DeviceDelegateImpl(uint16_t http_port, uint16_t https_port)
      : http_port_{http_port}, https_port_{https_port} {}
  ~DeviceDelegateImpl() override = default;

  std::pair<uint16_t, uint16_t> GetHttpEnpoint() const override {
    return std::make_pair(http_port_, http_port_);
  }
  std::pair<uint16_t, uint16_t> GetHttpsEnpoint() const override {
    return std::make_pair(https_port_, https_port_);
  }
  base::TimeDelta GetUptime() const override {
    return base::Time::Now() - start_time_;
  }

  void SetHttpPort(uint16_t port) override { http_port_ = port; }

  void SetHttpsPort(uint16_t port) override { https_port_ = port; }

 private:
  uint16_t http_port_{0};
  uint16_t https_port_{0};
  base::Time start_time_{base::Time::Now()};
};

}  // namespace

DeviceDelegate::DeviceDelegate() {}

DeviceDelegate::~DeviceDelegate() {}

// static
std::unique_ptr<DeviceDelegate> DeviceDelegate::CreateDefault(
    uint16_t http_port,
    uint16_t https_port) {
  return std::unique_ptr<DeviceDelegate>(
      new DeviceDelegateImpl(http_port, https_port));
}

}  // namespace privet
}  // namespace weave