// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/login_utils.h"

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/files/scoped_temp_dir.h"
#include "base/message_loop.h"
#include "base/path_service.h"
#include "base/string_util.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/sequenced_worker_pool.h"
#include "base/threading/thread.h"
#include "chrome/browser/chromeos/cros/cros_library.h"
#include "chrome/browser/chromeos/cros/mock_cryptohome_library.h"
#include "chrome/browser/chromeos/input_method/mock_input_method_manager.h"
#include "chrome/browser/chromeos/login/authenticator.h"
#include "chrome/browser/chromeos/login/login_status_consumer.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/settings/device_settings_test_helper.h"
#include "chrome/browser/io_thread.h"
#include "chrome/browser/net/predictor.h"
#include "chrome/browser/policy/browser_policy_connector.h"
#include "chrome/browser/policy/cloud_policy_data_store.h"
#include "chrome/browser/policy/policy_service.h"
#include "chrome/browser/policy/proto/device_management_backend.pb.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_pref_service.h"
#include "chromeos/cryptohome/mock_async_method_caller.h"
#include "chromeos/dbus/mock_cryptohome_client.h"
#include "chromeos/dbus/mock_dbus_thread_manager.h"
#include "chromeos/dbus/mock_session_manager_client.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_service.h"
#include "content/public/test/test_browser_thread.h"
#include "content/public/test/test_utils.h"
#include "google_apis/gaia/gaia_auth_consumer.h"
#include "google_apis/gaia/gaia_urls.h"
#include "net/url_request/test_url_fetcher_factory.h"
#include "net/url_request/url_fetcher_delegate.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_status.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace chromeos {

namespace {

namespace em = enterprise_management;

using ::testing::DoAll;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;
using ::testing::_;
using content::BrowserThread;

const char kTrue[] = "true";
const char kDomain[] = "domain.com";
const char kUsername[] = "user@domain.com";
const char kMode[] = "enterprise";
const char kDeviceId[] = "100200300";
const char kUsernameOtherDomain[] = "user@other.com";
const char kAttributeOwned[] = "enterprise.owned";
const char kAttributeOwner[] = "enterprise.user";
const char kAttrEnterpriseDomain[] = "enterprise.domain";
const char kAttrEnterpriseMode[] = "enterprise.mode";
const char kAttrEnterpriseDeviceId[] = "enterprise.device_id";

const char kOAuthTokenCookie[] = "oauth_token=1234";
const char kOAuthGetAccessTokenData[] =
    "oauth_token=1234&oauth_token_secret=1234";
const char kOAuthServiceTokenData[] =
    "wrap_access_token=1234&wrap_access_token_expires_in=123456789";

const char kDMServer[] = "http://server/device_management";
const char kDMRegisterRequest[] =
    "http://server/device_management?request=register";
const char kDMPolicyRequest[] =
    "http://server/device_management?request=policy";

const char kDMToken[] = "1234";

// Used to mark |flag|, indicating that RefreshPolicies() has executed its
// callback.
void SetFlag(bool* flag) {
  *flag = true;
}

// Single task of the fake IO loop used in the test, that just waits until
// it is signaled to quit or perform some work.
// |completion| is the event to wait for, and |work| is the task to invoke
// when signaled. If the task returns false then this quits the IO loop.
void BlockLoop(base::WaitableEvent* completion, base::Callback<bool()> work) {
  do {
    completion->Wait();
  } while (work.Run());
  MessageLoop::current()->QuitNow();
}

ACTION_P(MockSessionManagerClientRetrievePolicyCallback, policy) {
  arg0.Run(*policy);
}

ACTION_P(MockSessionManagerClientStorePolicyCallback, success) {
  arg1.Run(success);
}

class LoginUtilsTest : public testing::Test,
                       public LoginUtils::Delegate,
                       public LoginStatusConsumer {
 public:
  // Initialization here is important. The UI thread gets the test's
  // message loop, as does the file thread (which never actually gets
  // started - so this is a way to fake multiple threads on a single
  // test thread).  The IO thread does not get the message loop set,
  // and is never started.  This is necessary so that we skip various
  // bits of initialization that get posted to the IO thread.  We do
  // however, at one point in the test, temporarily set the message
  // loop for the IO thread.
  LoginUtilsTest()
      : fake_io_thread_completion_(false, false),
        fake_io_thread_("fake_io_thread"),
        loop_(MessageLoop::TYPE_IO),
        browser_process_(
            static_cast<TestingBrowserProcess*>(g_browser_process)),
        local_state_(browser_process_),
        ui_thread_(BrowserThread::UI, &loop_),
        db_thread_(BrowserThread::DB),
        file_thread_(BrowserThread::FILE, &loop_),
        mock_async_method_caller_(NULL),
        connector_(NULL),
        cryptohome_(NULL),
        prepared_profile_(NULL) {}

  virtual void SetUp() OVERRIDE {
    // This test is not a full blown InProcessBrowserTest, and doesn't have
    // all the usual threads running. However a lot of subsystems pulled from
    // ProfileImpl post to IO (usually from ProfileIOData), and DCHECK that
    // those tasks were posted. Those tasks in turn depend on a lot of other
    // components that aren't there during this test, so this kludge is used to
    // have a running IO loop that doesn't really execute any tasks.
    //
    // See InvokeOnIO() below for a way to perform specific tasks on IO, when
    // that's necessary.

    // A thread is needed to create a new MessageLoop, since there can be only
    // one loop per thread.
    fake_io_thread_.StartWithOptions(
        base::Thread::Options(MessageLoop::TYPE_IO, 0));
    MessageLoop* fake_io_loop = fake_io_thread_.message_loop();
    // Make this loop enter the single task, BlockLoop(). Pass in the completion
    // event and the work callback.
    fake_io_thread_.StopSoon();
    fake_io_loop->PostTask(
        FROM_HERE,
        base::Bind(
          BlockLoop,
          &fake_io_thread_completion_,
          base::Bind(&LoginUtilsTest::DoIOWork, base::Unretained(this))));
    // Map BrowserThread::IO to this loop. This allows posting to IO but nothing
    // will be executed.
    io_thread_.reset(
        new content::TestBrowserThread(BrowserThread::IO, fake_io_loop));

    ASSERT_TRUE(scoped_temp_dir_.CreateUniqueTempDir());

    CommandLine* command_line = CommandLine::ForCurrentProcess();
    command_line->AppendSwitchASCII(switches::kDeviceManagementUrl, kDMServer);
    command_line->AppendSwitchASCII(switches::kLoginProfile, "user");

    local_state_.Get()->RegisterStringPref(prefs::kApplicationLocale, "");

    // DBusThreadManager should be initialized before io_thread_state_, as
    // DBusThreadManager is used from chromeos::ProxyConfigServiceImpl,
    // which is part of io_thread_state_.
    DBusThreadManager::InitializeForTesting(&mock_dbus_thread_manager_);

    input_method::InputMethodManager::InitializeForTesting(
        &mock_input_method_manager_);

    // Likewise, SessionManagerClient should also be initialized before
    // io_thread_state_.
    MockSessionManagerClient* session_managed_client =
        mock_dbus_thread_manager_.mock_session_manager_client();
    EXPECT_CALL(*session_managed_client, RetrieveDevicePolicy(_))
        .WillRepeatedly(
            MockSessionManagerClientRetrievePolicyCallback(&device_policy_));
    EXPECT_CALL(*session_managed_client, RetrieveUserPolicy(_))
        .WillRepeatedly(
            MockSessionManagerClientRetrievePolicyCallback(&user_policy_));
    EXPECT_CALL(*session_managed_client, StoreUserPolicy(_, _))
        .WillRepeatedly(
            DoAll(SaveArg<0>(&user_policy_),
                  MockSessionManagerClientStorePolicyCallback(true)));

    mock_async_method_caller_ = new cryptohome::MockAsyncMethodCaller;
    cryptohome::AsyncMethodCaller::InitializeForTesting(
        mock_async_method_caller_);

    io_thread_state_.reset(new IOThread(local_state_.Get(), NULL, NULL));
    browser_process_->SetIOThread(io_thread_state_.get());

    CrosLibrary::TestApi* test_api = CrosLibrary::Get()->GetTestApi();
    ASSERT_TRUE(test_api);

    cryptohome_ = new MockCryptohomeLibrary();
    EXPECT_CALL(*cryptohome_, InstallAttributesIsReady())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*cryptohome_, InstallAttributesIsInvalid())
        .WillRepeatedly(Return(false));
    EXPECT_CALL(*cryptohome_, InstallAttributesIsFirstInstall())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*cryptohome_, TpmIsEnabled())
        .WillRepeatedly(Return(false));
    EXPECT_CALL(*cryptohome_, InstallAttributesSet(kAttributeOwned, kTrue))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*cryptohome_, InstallAttributesSet(kAttributeOwner,
                                                   kUsername))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*cryptohome_, InstallAttributesSet(kAttrEnterpriseDomain,
                                                   kDomain))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*cryptohome_, InstallAttributesSet(kAttrEnterpriseMode,
                                                   kMode))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*cryptohome_, InstallAttributesSet(kAttrEnterpriseDeviceId,
                                                   kDeviceId))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*cryptohome_, InstallAttributesFinalize())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*cryptohome_, InstallAttributesGet(kAttributeOwned, _))
        .WillRepeatedly(DoAll(SetArgPointee<1>(kTrue),
                              Return(true)));
    EXPECT_CALL(*cryptohome_, InstallAttributesGet(kAttributeOwner, _))
        .WillRepeatedly(DoAll(SetArgPointee<1>(kUsername),
                              Return(true)));
    EXPECT_CALL(*cryptohome_, InstallAttributesGet(kAttrEnterpriseDomain, _))
        .WillRepeatedly(DoAll(SetArgPointee<1>(kDomain),
                              Return(true)));
    EXPECT_CALL(*cryptohome_, InstallAttributesGet(kAttrEnterpriseMode, _))
        .WillRepeatedly(DoAll(SetArgPointee<1>(kMode),
                              Return(true)));
    EXPECT_CALL(*cryptohome_, InstallAttributesGet(kAttrEnterpriseDeviceId, _))
        .WillRepeatedly(DoAll(SetArgPointee<1>(kDeviceId),
                              Return(true)));
    test_api->SetCryptohomeLibrary(cryptohome_, true);

    EXPECT_CALL(*mock_dbus_thread_manager_.mock_cryptohome_client(),
                IsMounted(_));

    browser_process_->SetProfileManager(
        new ProfileManagerWithoutInit(scoped_temp_dir_.path()));
    connector_ = browser_process_->browser_policy_connector();
    connector_->Init();

    RunUntilIdle();
  }

  virtual void TearDown() OVERRIDE {
    cryptohome::AsyncMethodCaller::Shutdown();
    mock_async_method_caller_ = NULL;

    InvokeOnIO(
        base::Bind(&LoginUtilsTest::TearDownOnIO, base::Unretained(this)));

    // These trigger some tasks that have to run while BrowserThread::UI
    // exists. Delete all the profiles before deleting the connector.
    browser_process_->SetProfileManager(NULL);
    connector_ = NULL;
    browser_process_->SetBrowserPolicyConnector(NULL);
    QuitIOLoop();
    RunUntilIdle();
  }

  void TearDownOnIO() {
    // chrome_browser_net::Predictor usually skips its shutdown routines on
    // unit_tests, but does the full thing when
    // g_browser_process->profile_manager() is valid during initialization.
    // That includes a WaitableEvent on UI waiting for a task on IO, so that
    // task must execute. Do it directly from here now.
    std::vector<Profile*> profiles =
        browser_process_->profile_manager()->GetLoadedProfiles();
    for (size_t i = 0; i < profiles.size(); ++i) {
      chrome_browser_net::Predictor* predictor =
          profiles[i]->GetNetworkPredictor();
      if (predictor) {
        predictor->EnablePredictorOnIOThread(false);
        predictor->Shutdown();
      }
    }
  }

  void RunUntilIdle() {
    loop_.RunUntilIdle();
    BrowserThread::GetBlockingPool()->FlushForTesting();
    loop_.RunUntilIdle();
  }

  // Invokes |task| on the IO loop and returns after it has executed.
  void InvokeOnIO(const base::Closure& task) {
    fake_io_thread_work_ = task;
    fake_io_thread_completion_.Signal();
    content::RunMessageLoop();
  }

  // Makes the fake IO loop return.
  void QuitIOLoop() {
    fake_io_thread_completion_.Signal();
    content::RunMessageLoop();
  }

  // Helper for BlockLoop, InvokeOnIO and QuitIOLoop.
  bool DoIOWork() {
    bool has_work = !fake_io_thread_work_.is_null();
    if (has_work)
      fake_io_thread_work_.Run();
    fake_io_thread_work_.Reset();
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        MessageLoop::QuitWhenIdleClosure());
    // If there was work then keep waiting for more work.
    // If there was no work then quit the fake IO loop.
    return has_work;
  }

  virtual void OnProfilePrepared(Profile* profile) OVERRIDE {
    EXPECT_FALSE(prepared_profile_);
    prepared_profile_ = profile;
  }

  virtual void OnLoginFailure(const LoginFailure& error) OVERRIDE {
    FAIL() << "OnLoginFailure not expected";
  }

  virtual void OnLoginSuccess(const std::string& username,
                              const std::string& password,
                              bool pending_requests,
                              bool using_oauth) OVERRIDE {
    FAIL() << "OnLoginSuccess not expected";
  }

  void LockDevice(const std::string& username) {
    EXPECT_CALL(*cryptohome_, InstallAttributesIsFirstInstall())
        .WillOnce(Return(true))
        .WillRepeatedly(Return(false));
    policy::CloudPolicyDataStore* device_data_store =
        connector_->GetDeviceCloudPolicyDataStore();
    device_data_store->set_device_mode(policy::DEVICE_MODE_ENTERPRISE);
    device_data_store->set_device_id(kDeviceId);
    EXPECT_EQ(policy::EnterpriseInstallAttributes::LOCK_SUCCESS,
              connector_->LockDevice(username));
    RunUntilIdle();
  }

  void PrepareProfile(const std::string& username) {
    ScopedDeviceSettingsTestHelper device_settings_test_helper;
    MockSessionManagerClient* session_manager_client =
        mock_dbus_thread_manager_.mock_session_manager_client();
    EXPECT_CALL(*session_manager_client, StartSession(_));
    EXPECT_CALL(*cryptohome_, GetSystemSalt())
        .WillRepeatedly(Return(std::string("stub_system_salt")));
    EXPECT_CALL(*mock_async_method_caller_, AsyncMount(_, _, _, _))
        .WillRepeatedly(Return());

    scoped_refptr<Authenticator> authenticator =
        LoginUtils::Get()->CreateAuthenticator(this);
    authenticator->CompleteLogin(ProfileManager::GetDefaultProfile(),
                                 username,
                                 "password");

    const bool kPendingRequests = false;
    const bool kUsingOAuth = true;
    const bool kHasCookies = true;
    LoginUtils::Get()->PrepareProfile(username, std::string(), "password",
                                      kPendingRequests, kUsingOAuth,
                                      kHasCookies, this);
    device_settings_test_helper.Flush();
    RunUntilIdle();
  }

  net::TestURLFetcher* PrepareOAuthFetcher(const std::string& expected_url) {
    net::TestURLFetcher* fetcher = test_url_fetcher_factory_.GetFetcherByID(0);
    EXPECT_TRUE(fetcher);
    EXPECT_TRUE(fetcher->delegate());
    EXPECT_TRUE(StartsWithASCII(fetcher->GetOriginalURL().spec(),
                                expected_url,
                                true));
    fetcher->set_url(fetcher->GetOriginalURL());
    fetcher->set_response_code(200);
    fetcher->set_status(net::URLRequestStatus());
    return fetcher;
  }

  net::TestURLFetcher* PrepareDMServiceFetcher(
      const std::string& expected_url,
      const em::DeviceManagementResponse& response) {
    net::TestURLFetcher* fetcher = test_url_fetcher_factory_.GetFetcherByID(0);
    EXPECT_TRUE(fetcher);
    EXPECT_TRUE(fetcher->delegate());
    EXPECT_TRUE(StartsWithASCII(fetcher->GetOriginalURL().spec(),
                                expected_url,
                                true));
    fetcher->set_url(fetcher->GetOriginalURL());
    fetcher->set_response_code(200);
    fetcher->set_status(net::URLRequestStatus());
    std::string data;
    EXPECT_TRUE(response.SerializeToString(&data));
    fetcher->SetResponseString(data);
    return fetcher;
  }

  net::TestURLFetcher* PrepareDMRegisterFetcher() {
    em::DeviceManagementResponse response;
    em::DeviceRegisterResponse* register_response =
        response.mutable_register_response();
    register_response->set_device_management_token(kDMToken);
    register_response->set_enrollment_type(
        em::DeviceRegisterResponse::ENTERPRISE);
    return PrepareDMServiceFetcher(kDMRegisterRequest, response);
  }

  net::TestURLFetcher* PrepareDMPolicyFetcher() {
    em::DeviceManagementResponse response;
    response.mutable_policy_response()->add_response();
    return PrepareDMServiceFetcher(kDMPolicyRequest, response);
  }

 protected:
  ScopedStubCrosEnabler stub_cros_enabler_;

  base::Closure fake_io_thread_work_;
  base::WaitableEvent fake_io_thread_completion_;
  base::Thread fake_io_thread_;

  MessageLoop loop_;
  TestingBrowserProcess* browser_process_;
  ScopedTestingLocalState local_state_;

  content::TestBrowserThread ui_thread_;
  content::TestBrowserThread db_thread_;
  content::TestBrowserThread file_thread_;
  scoped_ptr<content::TestBrowserThread> io_thread_;
  scoped_ptr<IOThread> io_thread_state_;

  MockDBusThreadManager mock_dbus_thread_manager_;
  input_method::MockInputMethodManager mock_input_method_manager_;
  net::TestURLFetcherFactory test_url_fetcher_factory_;

  cryptohome::MockAsyncMethodCaller* mock_async_method_caller_;

  policy::BrowserPolicyConnector* connector_;
  MockCryptohomeLibrary* cryptohome_;
  Profile* prepared_profile_;

 private:
  base::ScopedTempDir scoped_temp_dir_;

  std::string device_policy_;
  std::string user_policy_;

  DISALLOW_COPY_AND_ASSIGN(LoginUtilsTest);
};

class LoginUtilsBlockingLoginTest
    : public LoginUtilsTest,
      public testing::WithParamInterface<int> {};

TEST_F(LoginUtilsTest, NormalLoginDoesntBlock) {
  UserManager* user_manager = UserManager::Get();
  ASSERT_TRUE(!user_manager->IsUserLoggedIn());
  EXPECT_FALSE(connector_->IsEnterpriseManaged());
  EXPECT_FALSE(prepared_profile_);

  // The profile will be created without waiting for a policy response.
  PrepareProfile(kUsername);

  EXPECT_TRUE(prepared_profile_);
  ASSERT_TRUE(user_manager->IsUserLoggedIn());
  EXPECT_EQ(kUsername, user_manager->GetLoggedInUser()->email());
}

TEST_F(LoginUtilsTest, EnterpriseLoginDoesntBlockForNormalUser) {
  UserManager* user_manager = UserManager::Get();
  ASSERT_TRUE(!user_manager->IsUserLoggedIn());
  EXPECT_FALSE(connector_->IsEnterpriseManaged());
  EXPECT_FALSE(prepared_profile_);

  // Enroll the device.
  LockDevice(kUsername);

  ASSERT_TRUE(!user_manager->IsUserLoggedIn());
  EXPECT_TRUE(connector_->IsEnterpriseManaged());
  EXPECT_EQ(kDomain, connector_->GetEnterpriseDomain());
  EXPECT_FALSE(prepared_profile_);

  // Login with a non-enterprise user shouldn't block.
  PrepareProfile(kUsernameOtherDomain);

  EXPECT_TRUE(prepared_profile_);
  ASSERT_TRUE(user_manager->IsUserLoggedIn());
  EXPECT_EQ(kUsernameOtherDomain, user_manager->GetLoggedInUser()->email());
}

TEST_F(LoginUtilsTest, OAuth1TokenFetchFailureUnblocksRefreshPolicies) {
  // 0. Check that a user is not logged in yet.
  UserManager* user_manager = UserManager::Get();
  ASSERT_TRUE(!user_manager->IsUserLoggedIn());
  EXPECT_FALSE(connector_->IsEnterpriseManaged());
  EXPECT_FALSE(prepared_profile_);

  // 1. Fake sign-in.
  // The profile will be created without waiting for a policy.
  content::WindowedNotificationObserver profile_creation_observer(
      chrome::NOTIFICATION_PROFILE_CREATED,
      content::NotificationService::AllSources());
  PrepareProfile(kUsername);
  // Wait until the profile is fully initialized. This makes sure the async
  // prefs init has finished, and the OnProfileCreated() callback has been
  // invoked.
  profile_creation_observer.Wait();
  EXPECT_TRUE(prepared_profile_);
  ASSERT_TRUE(user_manager->IsUserLoggedIn());
  EXPECT_EQ(kUsername, user_manager->GetLoggedInUser()->email());

  // 2. Get the pending oauth1 access token fetcher.
  net::TestURLFetcher* fetcher =
      PrepareOAuthFetcher(GaiaUrls::GetInstance()->get_oauth_token_url());
  ASSERT_TRUE(fetcher);

  // 3. Issuing a RefreshPolicies() now blocks waiting for the oauth token.
  bool refresh_policies_completed = false;
  browser_process_->policy_service()->RefreshPolicies(
      base::Bind(SetFlag, &refresh_policies_completed));
  RunUntilIdle();
  ASSERT_FALSE(refresh_policies_completed);

  // 4. Now make the fetcher fail. RefreshPolicies() should unblock.
  // The OAuth1TokenFetcher retries up to 5 times with a 3 second delay;
  // just invoke the callback directly to avoid waiting for that.
  // The |mock_fetcher| is passed instead of the original because the original
  // is deleted by the GaiaOAuthFetcher after the first callback.
  net::URLFetcherDelegate* delegate = fetcher->delegate();
  ASSERT_TRUE(delegate);
  net::TestURLFetcher mock_fetcher(fetcher->id(),
                                   fetcher->GetOriginalURL(),
                                   delegate);
  mock_fetcher.set_status(net::URLRequestStatus());
  mock_fetcher.set_response_code(404);
  for (int i = 0; i < 6; ++i) {
    ASSERT_FALSE(refresh_policies_completed);
    delegate->OnURLFetchComplete(&mock_fetcher);
    RunUntilIdle();
  }
  EXPECT_TRUE(refresh_policies_completed);
}

TEST_P(LoginUtilsBlockingLoginTest, EnterpriseLoginBlocksForEnterpriseUser) {
  UserManager* user_manager = UserManager::Get();
  ASSERT_TRUE(!user_manager->IsUserLoggedIn());
  EXPECT_FALSE(connector_->IsEnterpriseManaged());
  EXPECT_FALSE(prepared_profile_);

  // Enroll the device.
  LockDevice(kUsername);

  ASSERT_TRUE(!user_manager->IsUserLoggedIn());
  EXPECT_TRUE(connector_->IsEnterpriseManaged());
  EXPECT_EQ(kDomain, connector_->GetEnterpriseDomain());
  EXPECT_FALSE(prepared_profile_);

  // Login with a user of the enterprise domain waits for policy.
  PrepareProfile(kUsername);

  EXPECT_FALSE(prepared_profile_);
  ASSERT_TRUE(user_manager->IsUserLoggedIn());

  GaiaUrls* gaia_urls = GaiaUrls::GetInstance();
  net::TestURLFetcher* fetcher;

  // |steps| is the test parameter, and is the number of successful fetches.
  // The first incomplete fetch will fail. In any case, the profile creation
  // should resume.
  int steps = GetParam();

  do {
    if (steps < 1) break;

    // Fake OAuth token retrieval:
    fetcher = PrepareOAuthFetcher(gaia_urls->get_oauth_token_url());
    ASSERT_TRUE(fetcher);
    net::ResponseCookies cookies;
    cookies.push_back(kOAuthTokenCookie);
    fetcher->set_cookies(cookies);
    fetcher->delegate()->OnURLFetchComplete(fetcher);
    if (steps < 2) break;

    // Fake OAuth access token retrieval:
    fetcher = PrepareOAuthFetcher(gaia_urls->oauth_get_access_token_url());
    ASSERT_TRUE(fetcher);
    fetcher->SetResponseString(kOAuthGetAccessTokenData);
    fetcher->delegate()->OnURLFetchComplete(fetcher);
    if (steps < 3) break;

    // Fake OAuth service token retrieval:
    fetcher = PrepareOAuthFetcher(gaia_urls->oauth_wrap_bridge_url());
    ASSERT_TRUE(fetcher);
    fetcher->SetResponseString(kOAuthServiceTokenData);
    fetcher->delegate()->OnURLFetchComplete(fetcher);

    // The cloud policy subsystem is now ready to fetch the dmtoken and the user
    // policy.
    RunUntilIdle();
    if (steps < 4) break;

    fetcher = PrepareDMRegisterFetcher();
    ASSERT_TRUE(fetcher);
    fetcher->delegate()->OnURLFetchComplete(fetcher);
    // The policy fetch job has now been scheduled, run it:
    RunUntilIdle();
    if (steps < 5) break;

    // Verify that there is no profile prepared just before the policy fetch.
    EXPECT_FALSE(prepared_profile_);

    fetcher = PrepareDMPolicyFetcher();
    ASSERT_TRUE(fetcher);
    fetcher->delegate()->OnURLFetchComplete(fetcher);
  } while (0);

  if (steps < 5) {
    // Verify that the profile hasn't been created yet.
    EXPECT_FALSE(prepared_profile_);

    // Make the current fetcher fail.
    net::TestURLFetcher* fetcher = test_url_fetcher_factory_.GetFetcherByID(0);
    ASSERT_TRUE(fetcher);
    EXPECT_TRUE(fetcher->delegate());
    fetcher->set_url(fetcher->GetOriginalURL());
    fetcher->set_response_code(500);
    fetcher->delegate()->OnURLFetchComplete(fetcher);
  }

  // The profile is finally ready:
  EXPECT_TRUE(prepared_profile_);
}

INSTANTIATE_TEST_CASE_P(
    LoginUtilsBlockingLoginTestInstance,
    LoginUtilsBlockingLoginTest,
    testing::Values(0, 1, 2, 3, 4, 5));

}  // namespace

}
