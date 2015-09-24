// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <weave/device.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <weave/test/mock_bluetooth.h>
#include <weave/test/mock_config_store.h>
#include <weave/test/mock_dns_service_discovery_provider.h>
#include <weave/test/mock_http_client.h>
#include <weave/test/mock_http_server.h>
#include <weave/test/mock_network_provider.h>
#include <weave/test/mock_task_runner.h>
#include <weave/test/mock_wifi_provider.h>
#include <weave/test/unittest_utils.h>

#include "libweave/src/bind_lambda.h"

using testing::_;
using testing::AtMost;
using testing::HasSubstr;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::MatchesRegex;
using testing::Mock;
using testing::AtLeast;
using testing::Return;
using testing::ReturnRefOfCopy;
using testing::StartsWith;
using testing::StrictMock;
using testing::WithArgs;

namespace weave {

using test::CreateDictionaryValue;
using test::ValueToString;

const char kCategory[] = "powerd";

const char kBaseCommandDefs[] = R"({
  "base": {
    "reboot": {
      "parameters": {"delay": "integer"},
      "results": {}
    },
    "shutdown": {
      "parameters": {},
      "results": {}
    }
  }
})";

const char kCommandDefs[] = R"({
  "base": {
    "reboot": {},
    "shutdown": {}
  }
})";

const char kBaseStateDefs[] = R"({
  "base": {
    "firmwareVersion": "string",
    "localDiscoveryEnabled": "boolean",
    "localAnonymousAccessMaxRole": [ "none", "viewer", "user" ],
    "localPairingEnabled": "boolean",
    "network": {
      "properties": {
        "name": "string"
      }
    }
  }
})";

const char kBaseStateDefaults[] = R"({
  "base": {
    "firmwareVersion": "",
    "localDiscoveryEnabled": false,
    "localAnonymousAccessMaxRole": "none",
    "localPairingEnabled": false
  }
})";

const char kDeviceResource[] = R"({
  "kind": "clouddevices#device",
  "id": "DEVICE_ID",
  "channel": {
    "supportedType": "pull"
  },
  "deviceKind": "vendor",
  "modelManifestId": "ABCDE",
  "systemName": "",
  "name": "DEVICE_NAME",
  "displayName": "",
  "description": "Developer device",
  "stateValidationEnabled": true,
  "commandDefs":{
    "base": {
      "reboot": {
        "minimalRole": "user",
        "parameters": {"delay": "integer"},
        "results": {}
      },
      "shutdown": {
        "minimalRole": "user",
        "parameters": {},
        "results": {}
      }
    }
  },
  "state":{
    "base":{
      "firmwareVersion":"FIRMWARE_VERSION",
      "localAnonymousAccessMaxRole":"viewer",
      "localDiscoveryEnabled":true,
      "localPairingEnabled":true,
      "network":{
      }
    },
    "power": {"battery_level":44}
  }
})";

const char kRegistrationResponse[] = R"({
  "kind": "clouddevices#registrationTicket",
  "id": "TEST_ID",
  "deviceId": "DEVICE_ID",
  "oauthClientId": "CLIENT_ID",
  "userEmail": "USER@gmail.com",
  "creationTimeMs": "1440087183738",
  "expirationTimeMs": "1440087423738"
})";

const char kRegistrationFinalResponse[] = R"({
  "kind": "clouddevices#registrationTicket",
  "id": "TEST_ID",
  "deviceId": "DEVICE_ID",
  "oauthClientId": "CLIENT_ID",
  "userEmail": "USER@gmail.com",
  "robotAccountEmail": "ROBO@gmail.com",
  "robotAccountAuthorizationCode": "AUTH_CODE",
  "creationTimeMs": "1440087183738",
  "expirationTimeMs": "1440087423738"
})";

const char kAuthTokenResponse[] = R"({
  "access_token" : "ACCESS_TOKEN",
  "token_type" : "Bearer",
  "expires_in" : 3599,
  "refresh_token" : "REFRESH_TOKEN"
})";

const char kStateDefs[] = R"({"power": {"battery_level":"integer"}})";

const char kStateDefaults[] = R"({"power": {"battery_level":44}})";

MATCHER_P(MatchTxt, txt, "") {
  std::vector<std::string> txt_copy = txt;
  std::sort(txt_copy.begin(), txt_copy.end());
  std::vector<std::string> arg_copy = arg;
  std::sort(arg_copy.begin(), arg_copy.end());
  return (arg_copy == txt_copy);
}

class WeaveTest : public ::testing::Test {
 protected:
  void SetUp() override { device_ = weave::Device::Create(); }

  void ExpectRequest(const std::string& method,
                     const std::string& url,
                     const std::string& json_response) {
    EXPECT_CALL(http_client_, MockSendRequest(method, url, _, _, _))
        .WillOnce(InvokeWithoutArgs([json_response]() {
          test::MockHttpClientResponse* response =
              new StrictMock<test::MockHttpClientResponse>;
          EXPECT_CALL(*response, GetStatusCode())
              .Times(AtLeast(1))
              .WillRepeatedly(Return(200));
          EXPECT_CALL(*response, GetContentType())
              .Times(AtLeast(1))
              .WillRepeatedly(Return("application/json; charset=utf-8"));
          EXPECT_CALL(*response, GetData())
              .Times(AtLeast(1))
              .WillRepeatedly(ReturnRefOfCopy(json_response));
          return response;
        }));
  }

  void InitConfigStore() {
    EXPECT_CALL(config_store_, LoadDefaults(_))
        .WillOnce(Invoke([](weave::Settings* settings) {
          settings->api_key = "API_KEY";
          settings->client_secret = "CLIENT_SECRET";
          settings->client_id = "CLIENT_ID";
          settings->firmware_version = "FIRMWARE_VERSION";
          settings->name = "DEVICE_NAME";
          settings->model_id = "ABCDE";
          return true;
        }));
    EXPECT_CALL(config_store_, SaveSettings("")).WillRepeatedly(Return());

    EXPECT_CALL(config_store_, LoadBaseCommandDefs())
        .WillOnce(Return(kBaseCommandDefs));

    EXPECT_CALL(config_store_, LoadCommandDefs())
        .WillOnce(Return(
            std::map<std::string, std::string>{{kCategory, kCommandDefs}}));

    EXPECT_CALL(config_store_, LoadBaseStateDefs())
        .WillOnce(Return(kBaseStateDefs));

    EXPECT_CALL(config_store_, LoadStateDefs())
        .WillOnce(Return(
            std::map<std::string, std::string>{{kCategory, kStateDefs}}));

    EXPECT_CALL(config_store_, LoadBaseStateDefaults())
        .WillOnce(Return(kBaseStateDefaults));

    EXPECT_CALL(config_store_, LoadStateDefaults())
        .WillOnce(Return(std::vector<std::string>{kStateDefaults}));
  }

  void InitNetwork() {
    EXPECT_CALL(network_, AddConnectionChangedCallback(_))
        .WillRepeatedly(Invoke(
            [this](const NetworkProvider::ConnectionChangedCallback& cb) {
              network_callbacks_.push_back(cb);
            }));
    EXPECT_CALL(network_, GetConnectionState())
        .WillRepeatedly(Return(NetworkState::kOffline));
  }

  void InitDnsSd() {
    EXPECT_CALL(dns_sd_, GetId()).WillRepeatedly(Return("TEST_ID"));
    EXPECT_CALL(dns_sd_, PublishService(_, _, _)).WillRepeatedly(Return());
    EXPECT_CALL(dns_sd_, StopPublishing("_privet._tcp")).WillOnce(Return());
  }

  void InitDnsSdPublishing(bool registered, const std::string& flags) {
    std::vector<std::string> txt{{"id=TEST_ID"}, {"flags=" + flags},
                                 {"mmid=ABCDE"}, {"services=_base"},
                                 {"txtvers=3"},  {"ty=DEVICE_NAME"}};
    if (registered) {
      txt.push_back("gcd_id=DEVICE_ID");

      // During registration device may announce itself twice:
      // 1. with GCD ID but not connected (DB)
      // 2. with GCD ID and connected (BB)
      EXPECT_CALL(dns_sd_, PublishService("_privet._tcp", 11, MatchTxt(txt)))
          .Times(AtMost(1))
          .WillOnce(Return());

      txt[1] = "flags=BB";
    }

    EXPECT_CALL(dns_sd_, PublishService("_privet._tcp", 11, MatchTxt(txt)))
        .WillOnce(Return());
  }

  void InitHttpServer() {
    EXPECT_CALL(http_server_, GetHttpPort()).WillRepeatedly(Return(11));
    EXPECT_CALL(http_server_, GetHttpsPort()).WillRepeatedly(Return(12));
    EXPECT_CALL(http_server_, GetHttpsCertificateFingerprint())
        .WillRepeatedly(ReturnRefOfCopy(std::vector<uint8_t>{1, 2, 3}));
    EXPECT_CALL(http_server_, AddRequestHandler(_, _))
        .WillRepeatedly(Invoke([this](const std::string& path_prefix,
                                      const HttpServer::OnRequestCallback& cb) {
          http_server_request_cb_.push_back(cb);
        }));
    EXPECT_CALL(http_server_, AddOnStateChangedCallback(_))
        .WillRepeatedly(
            Invoke([this](const HttpServer::OnStateChangedCallback& cb) {
              http_server_changed_cb_.push_back(cb);
            }));
  }

  void InitDefaultExpectations() {
    InitConfigStore();
    InitNetwork();
    EXPECT_CALL(wifi_, StartAccessPoint(MatchesRegex("DEVICE_NAME.*prv")))
        .WillOnce(Return());
    InitHttpServer();
    InitDnsSd();
  }

  void StartDevice() {
    weave::Device::Options options;
    options.xmpp_enabled = false;

    device_->Start(options, &config_store_, &task_runner_, &http_client_,
                   &network_, &dns_sd_, &http_server_, &wifi_, &bluetooth_);

    cloud_ = device_->GetCloud();
    ASSERT_TRUE(cloud_);

    cloud_->GetDeviceInfo(
        base::Bind(
            [](const base::DictionaryValue& response) { ADD_FAILURE(); }),
        base::Bind([](const Error* error) {
          EXPECT_TRUE(error->HasError("gcd", "device_not_registered"));
        }));

    for (const auto& cb : http_server_changed_cb_)
      cb.Run(http_server_);

    task_runner_.Run();
  }

  void NotifyNetworkChanged(NetworkState state, base::TimeDelta delay) {
    EXPECT_CALL(network_, GetConnectionState()).WillRepeatedly(Return(state));
    for (const auto& cb : network_callbacks_) {
      task_runner_.PostDelayedTask(FROM_HERE, cb, delay);
    }
  }

  std::vector<HttpServer::OnStateChangedCallback> http_server_changed_cb_;
  std::vector<HttpServer::OnRequestCallback> http_server_request_cb_;

  StrictMock<test::MockConfigStore> config_store_;
  StrictMock<test::MockTaskRunner> task_runner_;
  StrictMock<test::MockHttpClient> http_client_;
  StrictMock<test::MockNetworkProvider> network_;
  StrictMock<test::MockDnsServiceDiscovery> dns_sd_;
  StrictMock<test::MockHttpServer> http_server_;
  StrictMock<test::MockWifiProvider> wifi_;
  StrictMock<test::MockBluetooth> bluetooth_;

  std::vector<NetworkProvider::ConnectionChangedCallback> network_callbacks_;

  weave::Cloud* cloud_{nullptr};

  std::unique_ptr<weave::Device> device_;
};

TEST_F(WeaveTest, Create) {
  ASSERT_TRUE(device_.get());
}

TEST_F(WeaveTest, StartMinimal) {
  weave::Device::Options options;
  options.xmpp_enabled = false;
  options.disable_privet = true;
  options.disable_security = true;

  InitConfigStore();
  device_->Start(options, &config_store_, &task_runner_, &http_client_,
                 &network_, nullptr, nullptr, &wifi_, nullptr);
}

TEST_F(WeaveTest, StartNoWifi) {
  InitConfigStore();
  InitNetwork();
  InitHttpServer();
  InitDnsSd();
  InitDnsSdPublishing(false, "CB");

  weave::Device::Options options;
  device_->Start(options, &config_store_, &task_runner_, &http_client_,
                 &network_, &dns_sd_, &http_server_, nullptr, &bluetooth_);

  for (const auto& cb : http_server_changed_cb_)
    cb.Run(http_server_);

  task_runner_.Run();
}

class WeaveBasicTest : public WeaveTest {
 public:
  void SetUp() override {
    WeaveTest::SetUp();

    InitDefaultExpectations();
    InitDnsSdPublishing(false, "DB");
  }
};

TEST_F(WeaveBasicTest, Start) {
  StartDevice();
}

TEST_F(WeaveBasicTest, Register) {
  StartDevice();

  auto draft = CreateDictionaryValue(kDeviceResource);
  auto response = CreateDictionaryValue(kRegistrationResponse);
  response->Set("deviceDraft", draft->DeepCopy());
  ExpectRequest(
      "PATCH",
      "https://www.googleapis.com/clouddevices/v1/registrationTickets/"
      "TEST_ID?key=API_KEY",
      ValueToString(*response));

  response = CreateDictionaryValue(kRegistrationFinalResponse);
  response->Set("deviceDraft", draft->DeepCopy());
  ExpectRequest(
      "POST",
      "https://www.googleapis.com/clouddevices/v1/registrationTickets/"
      "TEST_ID/finalize?key=API_KEY",
      ValueToString(*response));

  ExpectRequest("POST", "https://accounts.google.com/o/oauth2/token",
                kAuthTokenResponse);

  InitDnsSdPublishing(true, "DB");

  EXPECT_EQ("DEVICE_ID", cloud_->RegisterDevice("TEST_ID", nullptr));
}

class WeaveWiFiSetupTest : public WeaveTest {
 public:
  void SetUp() override {
    WeaveTest::SetUp();

    InitConfigStore();
    InitHttpServer();
    InitNetwork();
    InitDnsSd();

    EXPECT_CALL(network_, GetConnectionState())
        .WillRepeatedly(Return(NetworkState::kConnected));
  }
};

TEST_F(WeaveWiFiSetupTest, StartOnlineNoPrevSsid) {
  StartDevice();

  // Short disconnect.
  NotifyNetworkChanged(NetworkState::kOffline, {});
  NotifyNetworkChanged(NetworkState::kConnected,
                       base::TimeDelta::FromSeconds(10));
  task_runner_.Run();

  // Long disconnect.
  NotifyNetworkChanged(NetworkState::kOffline, {});
  auto offline_from = task_runner_.GetClock()->Now();
  EXPECT_CALL(wifi_, StartAccessPoint(MatchesRegex("DEVICE_NAME.*prv")))
      .WillOnce(InvokeWithoutArgs([this, offline_from]() {
        EXPECT_GT(task_runner_.GetClock()->Now() - offline_from,
                  base::TimeDelta::FromMinutes(1));
        task_runner_.Break();
      }));
  task_runner_.Run();
}

// If device has previously configured WiFi it will run AP for limited time
// after which it will try to re-connect.
TEST_F(WeaveWiFiSetupTest, StartOnlineWithPrevSsid) {
  EXPECT_CALL(config_store_, LoadSettings())
      .WillRepeatedly(Return(R"({"last_configured_ssid": "TEST_ssid"})"));
  StartDevice();

  // Long disconnect.
  NotifyNetworkChanged(NetworkState::kOffline, {});

  for (int i = 0; i < 5; ++i) {
    auto offline_from = task_runner_.GetClock()->Now();
    // Temporarily offline mode.
    EXPECT_CALL(wifi_, StartAccessPoint(MatchesRegex("DEVICE_NAME.*prv")))
        .WillOnce(InvokeWithoutArgs([this, &offline_from]() {
          EXPECT_GT(task_runner_.GetClock()->Now() - offline_from,
                    base::TimeDelta::FromMinutes(1));
          task_runner_.Break();
        }));
    task_runner_.Run();

    // Try to reconnect again.
    offline_from = task_runner_.GetClock()->Now();
    EXPECT_CALL(wifi_, StopAccessPoint())
        .WillOnce(InvokeWithoutArgs([this, offline_from]() {
          EXPECT_GT(task_runner_.GetClock()->Now() - offline_from,
                    base::TimeDelta::FromMinutes(5));
          task_runner_.Break();
        }));
    task_runner_.Run();
  }

  NotifyNetworkChanged(NetworkState::kConnected, {});
  task_runner_.Run();
}

TEST_F(WeaveWiFiSetupTest, StartOfflineWithSsid) {
  EXPECT_CALL(config_store_, LoadSettings())
      .WillRepeatedly(Return(R"({"last_configured_ssid": "TEST_ssid"})"));
  EXPECT_CALL(network_, GetConnectionState())
      .WillRepeatedly(Return(NetworkState::kOffline));

  auto offline_from = task_runner_.GetClock()->Now();
  EXPECT_CALL(wifi_, StartAccessPoint(MatchesRegex("DEVICE_NAME.*prv")))
      .WillOnce(InvokeWithoutArgs([this, &offline_from]() {
        EXPECT_GT(task_runner_.GetClock()->Now() - offline_from,
                  base::TimeDelta::FromMinutes(1));
        task_runner_.Break();
      }));

  StartDevice();
}

}  // namespace weave
