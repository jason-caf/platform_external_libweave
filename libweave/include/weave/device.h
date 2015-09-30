// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBWEAVE_INCLUDE_WEAVE_DEVICE_H_
#define LIBWEAVE_INCLUDE_WEAVE_DEVICE_H_

#include <memory>
#include <set>
#include <string>

#include <weave/cloud.h>
#include <weave/commands.h>
#include <weave/export.h>
#include <weave/privet.h>
#include <weave/provider/bluetooth.h>
#include <weave/provider/config_store.h>
#include <weave/provider/dns_service_discovery.h>
#include <weave/provider/http_client.h>
#include <weave/provider/http_server.h>
#include <weave/provider/network.h>
#include <weave/provider/task_runner.h>
#include <weave/provider/wifi.h>
#include <weave/state.h>

namespace weave {

class Device {
 public:
  // Callback type for AddSettingsChangedCallback.
  using SettingsChangedCallback =
      base::Callback<void(const Settings& settings)>;

  virtual ~Device() = default;

  virtual void Start(provider::ConfigStore* config_store,
                     provider::TaskRunner* task_runner,
                     provider::HttpClient* http_client,
                     provider::Network* network,
                     provider::DnsServiceDiscovery* dns_sd,
                     provider::HttpServer* http_server,
                     provider::Wifi* wifi,
                     provider::Bluetooth* bluetooth_provider) = 0;

  // Returns reference the current settings.
  virtual const Settings& GetSettings() = 0;

  // Subscribes to notification settings changes.
  virtual void AddSettingsChangedCallback(
      const SettingsChangedCallback& callback) = 0;

  virtual Commands* GetCommands() = 0;
  virtual State* GetState() = 0;
  virtual Cloud* GetCloud() = 0;
  virtual Privet* GetPrivet() = 0;

  LIBWEAVE_EXPORT static std::unique_ptr<Device> Create();
};

}  // namespace weave

#endif  // LIBWEAVE_INCLUDE_WEAVE_DEVICE_H_
