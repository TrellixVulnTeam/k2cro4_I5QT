// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/base_paths.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/scoped_ptr.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/stringprintf.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/policy/browser_policy_connector.h"
#include "chrome/browser/policy/cloud_policy_client.h"
#include "chrome/browser/policy/cloud_policy_data_store.h"
#include "chrome/browser/policy/policy_map.h"
#include "chrome/browser/policy/policy_service.h"
#include "chrome/browser/policy/user_cloud_policy_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"
#include "content/public/test/test_utils.h"
#include "googleurl/src/gurl.h"
#include "net/test/test_server.h"
#include "policy/policy_constants.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/login/user_manager.h"
#else
#include "chrome/browser/signin/signin_manager.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#endif

using testing::InvokeWithoutArgs;
using testing::Mock;
using testing::_;

namespace policy {

namespace {

class MockCloudPolicyClientObserver : public CloudPolicyClient::Observer {
 public:
  MockCloudPolicyClientObserver() {}
  virtual ~MockCloudPolicyClientObserver() {}

  MOCK_METHOD1(OnPolicyFetched, void(CloudPolicyClient*));
  MOCK_METHOD1(OnRegistrationStateChanged, void(CloudPolicyClient*));
  MOCK_METHOD1(OnClientError, void(CloudPolicyClient*));
};

const char* GetTestUser() {
#if defined(OS_CHROMEOS)
  return chromeos::UserManager::kStubUser;
#else
  return "user@example.com";
#endif
}

std::string GetEmptyPolicy() {
  const char kEmptyPolicy[] =
      "{"
      "  \"google/chromeos/user\": {"
      "    \"mandatory\": {},"
      "    \"recommended\": {}"
      "  },"
      "  \"managed_users\": [ \"*\" ],"
      "  \"policy_user\": \"%s\""
      "}";

  return base::StringPrintf(kEmptyPolicy, GetTestUser());
}

std::string GetTestPolicy() {
  const char kTestPolicy[] =
      "{"
      "  \"google/chromeos/user\": {"
      "    \"mandatory\": {"
      "      \"ShowHomeButton\": true,"
      "      \"MaxConnectionsPerProxy\": 42,"
      "      \"URLBlacklist\": [ \"dev.chromium.org\", \"youtube.com\" ]"
      "    },"
      "    \"recommended\": {"
      "      \"HomepageLocation\": \"google.com\""
      "    }"
      "  },"
      "  \"managed_users\": [ \"*\" ],"
      "  \"policy_user\": \"%s\""
      "}";

  return base::StringPrintf(kTestPolicy, GetTestUser());
}

#if defined(OS_CHROMEOS)
void SetUpOldStackAfterCreatingBrowser(Browser* browser) {
  // Flush the token cache loading.
  content::RunAllPendingInMessageLoop(content::BrowserThread::FILE);
  content::RunAllPendingInMessageLoop();
  // Set a fake gaia token.
  BrowserPolicyConnector* connector =
      g_browser_process->browser_policy_connector();
  CloudPolicyDataStore* store = connector->GetUserCloudPolicyDataStore();
  ASSERT_TRUE(store);
  store->SetupForTesting("", "bogus", GetTestUser(), "bogus", true);
}
#endif

void SetUpNewStackBeforeCreatingBrowser() {
  CommandLine* command_line = CommandLine::ForCurrentProcess();
  command_line->AppendSwitch(switches::kEnableCloudPolicyService);
  command_line->AppendSwitch(switches::kLoadCloudPolicyOnSignin);
}

void SetUpNewStackAfterCreatingBrowser(Browser* browser) {
#if !defined(OS_CHROMEOS)
  // Mock a signed-in user. This is used by the UserCloudPolicyStore to pass the
  // username to the UserCloudPolicyValidator.
  SigninManager* signin_manager =
      SigninManagerFactory::GetForProfile(browser->profile());
  ASSERT_TRUE(signin_manager);
  signin_manager->SetAuthenticatedUsername(GetTestUser());
#endif

  BrowserPolicyConnector* connector =
      g_browser_process->browser_policy_connector();
  connector->ScheduleServiceInitialization(0);

  UserCloudPolicyManager* policy_manager =
      browser->profile()->GetUserCloudPolicyManager();
  ASSERT_TRUE(policy_manager);
  policy_manager->Initialize(g_browser_process->local_state(),
                             connector->device_management_service(),
                             policy::USER_AFFILIATION_MANAGED);

  ASSERT_TRUE(policy_manager->cloud_policy_client());
  base::RunLoop run_loop;
  MockCloudPolicyClientObserver observer;
  EXPECT_CALL(observer, OnRegistrationStateChanged(_)).WillOnce(
      InvokeWithoutArgs(&run_loop, &base::RunLoop::Quit));
  policy_manager->cloud_policy_client()->AddObserver(&observer);

  // Give a bogus OAuth token to the |policy_manager|. This should make its
  // CloudPolicyClient fetch the DMToken.
  policy_manager->RegisterClient("bogus");
  run_loop.Run();
  Mock::VerifyAndClearExpectations(&observer);
  policy_manager->cloud_policy_client()->RemoveObserver(&observer);
}

struct TestSetup {
  TestSetup(void (*before)(), void (*after)(Browser*))
      : SetUpBeforeCreatingBrowser(before),
        SetUpAfterCreatingBrowser(after) {}

  void (*SetUpBeforeCreatingBrowser)();
  void (*SetUpAfterCreatingBrowser)(Browser*);
};

}  // namespace

// Tests the cloud policy stack(s).
class CloudPolicyTest : public InProcessBrowserTest,
                        public testing::WithParamInterface<TestSetup> {
 protected:
  CloudPolicyTest() {}
  virtual ~CloudPolicyTest() {}

  virtual void SetUpInProcessBrowserTestFixture() OVERRIDE {
    // The TestServer wants the docroot as a path relative to the source dir.
    FilePath source;
    ASSERT_TRUE(PathService::Get(base::DIR_SOURCE_ROOT, &source));
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDirUnderPath(source));
    ASSERT_NO_FATAL_FAILURE(SetServerPolicy(GetEmptyPolicy()));

    test_server_.reset(
        new net::TestServer(
            net::TestServer::TYPE_HTTP,
            net::TestServer::kLocalhost,
            temp_dir_.path().BaseName()));
    ASSERT_TRUE(test_server_->Start());

    std::string url = test_server_->GetURL("device_management").spec();

    CommandLine* command_line = CommandLine::ForCurrentProcess();
    command_line->AppendSwitchASCII(switches::kDeviceManagementUrl, url);

    const TestSetup& setup = GetParam();
    setup.SetUpBeforeCreatingBrowser();
  }

  virtual void SetUpOnMainThread() OVERRIDE {
    GetParam().SetUpAfterCreatingBrowser(browser());
  }

  void SetServerPolicy(const std::string& policy) {
    int result = file_util::WriteFile(
        temp_dir_.path().AppendASCII("device_management"),
        policy.data(), policy.size());
    ASSERT_EQ(static_cast<int>(policy.size()), result);
  }

  base::ScopedTempDir temp_dir_;
  scoped_ptr<net::TestServer> test_server_;
};

IN_PROC_BROWSER_TEST_P(CloudPolicyTest, FetchPolicy) {
  PolicyService* policy_service = browser()->profile()->GetPolicyService();
  {
    base::RunLoop run_loop;
    policy_service->RefreshPolicies(run_loop.QuitClosure());
    run_loop.Run();
  }

  PolicyMap empty;
  EXPECT_TRUE(
      empty.Equals(policy_service->GetPolicies(POLICY_DOMAIN_CHROME, "")));

  ASSERT_NO_FATAL_FAILURE(SetServerPolicy(GetTestPolicy()));
  PolicyMap expected;
  expected.Set(key::kShowHomeButton, POLICY_LEVEL_MANDATORY, POLICY_SCOPE_USER,
               base::Value::CreateBooleanValue(true));
  expected.Set(key::kMaxConnectionsPerProxy, POLICY_LEVEL_MANDATORY,
               POLICY_SCOPE_USER, base::Value::CreateIntegerValue(42));
  base::ListValue list;
  list.AppendString("dev.chromium.org");
  list.AppendString("youtube.com");
  expected.Set(
      key::kURLBlacklist, POLICY_LEVEL_MANDATORY, POLICY_SCOPE_USER,
      list.DeepCopy());
  expected.Set(
      key::kHomepageLocation, POLICY_LEVEL_RECOMMENDED,
      POLICY_SCOPE_USER, base::Value::CreateStringValue("google.com"));
  {
    base::RunLoop run_loop;
    policy_service->RefreshPolicies(run_loop.QuitClosure());
    run_loop.Run();
  }
  EXPECT_TRUE(
      expected.Equals(policy_service->GetPolicies(POLICY_DOMAIN_CHROME, "")));
}

#if defined(OS_CHROMEOS)
INSTANTIATE_TEST_CASE_P(
    OldStackCloudPolicyTest,
    CloudPolicyTest,
    testing::Values(
        TestSetup(base::DoNothing, SetUpOldStackAfterCreatingBrowser)));
#endif

INSTANTIATE_TEST_CASE_P(
    NewStackCloudPolicyTest,
    CloudPolicyTest,
    testing::Values(
        TestSetup(SetUpNewStackBeforeCreatingBrowser,
                  SetUpNewStackAfterCreatingBrowser)));

}  // namespace policy
