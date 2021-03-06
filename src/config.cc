// Copyright 2015 The Weave Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/config.h"


#include <algorithm>
#include <set>

#include <base/bind.h>
#include <base/guid.h>
#include <base/json/json_reader.h>
#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/values.h>
#include <weave/enum_to_string.h>

#include "src/data_encoding.h"
#include "src/privet/privet_types.h"
#include "src/string_utils.h"
#include "src/bind_lambda.h"

namespace weave {

const char kConfigName[] = "config";

namespace config_keys {

const char kVersion[] = "version";

const char kClientId[] = "client_id";
const char kClientSecret[] = "client_secret";
const char kApiKey[] = "api_key";
const char kOAuthUrl[] = "oauth_url";
const char kServiceUrl[] = "service_url";
const char kXmppEndpoint[] = "xmpp_endpoint";
const char kName[] = "name";
const char kDescription[] = "description";
const char kLocation[] = "location";
const char kLocalAnonymousAccessRole[] = "local_anonymous_access_role";
const char kLocalAccessEnabled[] = "local_access_enabled";
const char kRefreshToken[] = "refresh_token";
const char kCloudId[] = "cloud_id";
const char kDeviceId[] = "device_id";
const char kRobotAccount[] = "robot_account";
const char kLastConfiguredSsid[] = "last_configured_ssid";
const char kSecret[] = "secret";
const char kRootClientTokenOwner[] = "root_client_token_owner";

}  // namespace config_keys

const char kWeaveUrl[] = "https://www.googleapis.com/weave/v1/";
const char kDeprecatedUrl[] = "https://www.googleapis.com/clouddevices/v1/";
const char kXmppEndpoint[] = "talk.google.com:5223";

namespace {

const int kCurrentConfigVersion = 2;

void MigrateFromV0(base::DictionaryValue* dict) {
  std::string cloud_id;
  if (dict->GetString(config_keys::kCloudId, &cloud_id) && !cloud_id.empty())
    return;
  std::unique_ptr<base::Value> tmp;
  if (dict->Remove(config_keys::kDeviceId, &tmp))
    dict->Set(config_keys::kCloudId, std::move(tmp));
}

void MigrateFromV1(base::DictionaryValue* dict) {
  // "local_discovery_enabled" and "local_pairing_enabled" are merged into one
  // setting: "local_access_enabled".
  std::unique_ptr<base::Value> bool_val;
  // Use the value of "local_discovery_enabled" for "local_access_enabled" and
  // remove "local_pairing_enabled".
  if (dict->Remove("local_discovery_enabled", &bool_val))
    dict->Set(config_keys::kLocalAccessEnabled, std::move(bool_val));
  dict->Remove("local_pairing_enabled", nullptr);
}

Config::Settings CreateDefaultSettings(provider::ConfigStore* config_store) {
  Config::Settings result;
  result.oauth_url = "https://accounts.google.com/o/oauth2/";
  result.service_url = kWeaveUrl;
  result.xmpp_endpoint = kXmppEndpoint;
  result.local_anonymous_access_role = AuthScope::kViewer;
  result.pairing_modes.insert(PairingType::kPinCode);
  result.device_id = base::GenerateGUID();

  if (!config_store)
    return result;

  // Crash on any mistakes in defaults.
  CHECK(config_store->LoadDefaults(&result));

  CHECK(!result.client_id.empty());
  CHECK(!result.client_secret.empty());
  CHECK(!result.api_key.empty());
  CHECK(!result.oauth_url.empty());
  CHECK(!result.service_url.empty());
  CHECK(!result.xmpp_endpoint.empty());
  CHECK(!result.oem_name.empty());
  CHECK(!result.model_name.empty());
  CHECK(!result.model_id.empty());
  CHECK(!result.name.empty());
  CHECK(!result.device_id.empty());
  CHECK_EQ(result.embedded_code.empty(),
           (result.pairing_modes.find(PairingType::kEmbeddedCode) ==
            result.pairing_modes.end()));

  // Values below will be generated at runtime.
  CHECK(result.cloud_id.empty());
  CHECK(result.refresh_token.empty());
  CHECK(result.robot_account.empty());
  CHECK(result.last_configured_ssid.empty());
  CHECK(result.secret.empty());
  CHECK(result.root_client_token_owner == RootClientTokenOwner::kNone);

  return result;
}

const EnumToStringMap<RootClientTokenOwner>::Map kRootClientTokenOwnerMap[] = {
    {RootClientTokenOwner::kNone, "none"},
    {RootClientTokenOwner::kClient, "client"},
    {RootClientTokenOwner::kCloud, "cloud"},
};

}  // namespace

template <>
LIBWEAVE_EXPORT EnumToStringMap<RootClientTokenOwner>::EnumToStringMap()
    : EnumToStringMap(kRootClientTokenOwnerMap) {}

Config::Config(provider::ConfigStore* config_store)
    : defaults_{CreateDefaultSettings(config_store)},
      settings_{defaults_},
      config_store_{config_store} {
  Transaction change{this};
  change.save_ = false;
  change.LoadState();
}

void Config::AddOnChangedCallback(const OnChangedCallback& callback) {
  on_changed_.push_back(callback);
  // Force to read current state.
  callback.Run(settings_);
}

const Config::Settings& Config::GetSettings() const {
  return settings_;
}

const Config::Settings& Config::GetDefaults() const {
  return defaults_;
}

void Config::Transaction::LoadState() {
  if (!config_->config_store_)
    return;
  std::string json_string = config_->config_store_->LoadSettings(kConfigName);
  if (json_string.empty()) {
    json_string = config_->config_store_->LoadSettings();
    if (json_string.empty())
      return;
  }

  auto value = base::JSONReader::Read(json_string);
  base::DictionaryValue* dict = nullptr;
  if (!value || !value->GetAsDictionary(&dict)) {
    LOG(ERROR) << "Failed to parse settings.";
    return;
  }

  int loaded_version = 0;
  dict->GetInteger(config_keys::kVersion, &loaded_version);

  if (loaded_version != kCurrentConfigVersion) {
    LOG(INFO) << "State version mismatch. expected: " << kCurrentConfigVersion
              << ", loaded: " << loaded_version;
    save_ = true;
  }

  switch (loaded_version) {
    case 0:
      MigrateFromV0(dict);
      break;
    case 1:
      MigrateFromV1(dict);
      break;
  }

  std::string tmp;
  bool tmp_bool{false};

  if (dict->GetString(config_keys::kClientId, &tmp))
    set_client_id(tmp);

  if (dict->GetString(config_keys::kClientSecret, &tmp))
    set_client_secret(tmp);

  if (dict->GetString(config_keys::kApiKey, &tmp))
    set_api_key(tmp);

  if (dict->GetString(config_keys::kOAuthUrl, &tmp))
    set_oauth_url(tmp);

  if (dict->GetString(config_keys::kServiceUrl, &tmp)) {
    if (tmp == kDeprecatedUrl)
      tmp = kWeaveUrl;
    set_service_url(tmp);
  }

  if (dict->GetString(config_keys::kXmppEndpoint, &tmp)) {
    set_xmpp_endpoint(tmp);
  }

  if (dict->GetString(config_keys::kName, &tmp))
    set_name(tmp);

  if (dict->GetString(config_keys::kDescription, &tmp))
    set_description(tmp);

  if (dict->GetString(config_keys::kLocation, &tmp))
    set_location(tmp);

  AuthScope scope{AuthScope::kNone};
  if (dict->GetString(config_keys::kLocalAnonymousAccessRole, &tmp) &&
      StringToEnum(tmp, &scope)) {
    set_local_anonymous_access_role(scope);
  }

  if (dict->GetBoolean(config_keys::kLocalAccessEnabled, &tmp_bool))
    set_local_access_enabled(tmp_bool);

  if (dict->GetString(config_keys::kCloudId, &tmp))
    set_cloud_id(tmp);

  if (dict->GetString(config_keys::kDeviceId, &tmp))
    set_device_id(tmp);

  if (dict->GetString(config_keys::kRefreshToken, &tmp))
    set_refresh_token(tmp);

  if (dict->GetString(config_keys::kRobotAccount, &tmp))
    set_robot_account(tmp);

  if (dict->GetString(config_keys::kLastConfiguredSsid, &tmp))
    set_last_configured_ssid(tmp);

  std::vector<uint8_t> secret;
  if (dict->GetString(config_keys::kSecret, &tmp) && Base64Decode(tmp, &secret))
    set_secret(secret);

  RootClientTokenOwner token_owner{RootClientTokenOwner::kNone};
  if (dict->GetString(config_keys::kRootClientTokenOwner, &tmp) &&
      StringToEnum(tmp, &token_owner)) {
    set_root_client_token_owner(token_owner);
  }
}

void Config::Save() {
  if (!config_store_)
    return;

  base::DictionaryValue dict;
  dict.SetInteger(config_keys::kVersion, kCurrentConfigVersion);

  dict.SetString(config_keys::kClientId, settings_.client_id);
  dict.SetString(config_keys::kClientSecret, settings_.client_secret);
  dict.SetString(config_keys::kApiKey, settings_.api_key);
  dict.SetString(config_keys::kOAuthUrl, settings_.oauth_url);
  dict.SetString(config_keys::kServiceUrl, settings_.service_url);
  dict.SetString(config_keys::kXmppEndpoint, settings_.xmpp_endpoint);
  dict.SetString(config_keys::kRefreshToken, settings_.refresh_token);
  dict.SetString(config_keys::kCloudId, settings_.cloud_id);
  dict.SetString(config_keys::kDeviceId, settings_.device_id);
  dict.SetString(config_keys::kRobotAccount, settings_.robot_account);
  dict.SetString(config_keys::kLastConfiguredSsid,
                 settings_.last_configured_ssid);
  dict.SetString(config_keys::kSecret, Base64Encode(settings_.secret));
  dict.SetString(config_keys::kRootClientTokenOwner,
                 EnumToString(settings_.root_client_token_owner));
  dict.SetString(config_keys::kName, settings_.name);
  dict.SetString(config_keys::kDescription, settings_.description);
  dict.SetString(config_keys::kLocation, settings_.location);
  dict.SetString(config_keys::kLocalAnonymousAccessRole,
                 EnumToString(settings_.local_anonymous_access_role));
  dict.SetBoolean(config_keys::kLocalAccessEnabled,
                  settings_.local_access_enabled);

  std::string json_string;
  base::JSONWriter::WriteWithOptions(
      dict, base::JSONWriter::OPTIONS_PRETTY_PRINT, &json_string);

  config_store_->SaveSettings(
      kConfigName, json_string,
      base::Bind([](ErrorPtr error) { CHECK(!error); }));
}

Config::Transaction::~Transaction() {
  Commit();
}

void Config::Transaction::Commit() {
  if (!config_)
    return;
  if (save_)
    config_->Save();
  for (const auto& cb : config_->on_changed_)
    cb.Run(*settings_);
  config_ = nullptr;
}

}  // namespace weave
