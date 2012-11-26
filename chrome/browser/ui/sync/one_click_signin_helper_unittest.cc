// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/content_settings/cookie_settings.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/prefs/scoped_user_pref_update.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_info_cache.h"
#include "chrome/browser/profiles/profile_io_data.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "chrome/browser/signin/signin_manager_fake.h"
#include "chrome/browser/signin/signin_names_io_thread.h"
#include "chrome/browser/sync/profile_sync_service_mock.h"
#include "chrome/browser/sync/profile_sync_service_factory.h"
#include "chrome/browser/sync/test_profile_sync_service.h"
#include "chrome/browser/ui/sync/one_click_signin_helper.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_pref_service.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/base/testing_profile_manager.h"

#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/test_browser_thread.h"
#include "content/public/test/test_renderer_host.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::_;
using ::testing::Mock;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::Values;

namespace {

// Explicit URLs are sign in URLs created by chrome for specific sign in access
// points.  Implicit URLs are those to sign for some Google service, like gmail
// or drive.  In former case, with a valid URL, we don't want to offer the
// interstitial.  In all other cases we do.

const char kImplicitURLString[] =
    "https://accounts.google.com/ServiceLogin"
    "?service=foo&continue=http://foo.google.com";

const char kExplicitURLString[] =
    "https://accounts.google.com/ServiceLogin"
    "?service=chromiumsync"
    "&continue=chrome-extension://foo/success.html?source=0";

const char kBad1ExplicitURLString[] =
    "https://accounts.google.com/ServiceLogin"
    "?service=foo"
    "&continue=chrome-extension://foo/success.html?source=0";

const char kBad2ExplicitURLString[] =
    "https://accounts.google.com/ServiceLogin"
    "?service=chromiumsync"
    "&continue=chrome-extension://foo/success.html?source=10";

const char kBad3ExplicitURLString[] =
    "https://accounts.google.com/ServiceLogin"
    "?service=chromiumsync"
    "&continue=chrome-extension://foo/success.html";

class SigninManagerMock : public FakeSigninManager {
 public:
  explicit SigninManagerMock(Profile* profile)
      : FakeSigninManager(profile) {}
  MOCK_CONST_METHOD1(IsAllowedUsername, bool(const std::string& username));
};

class TestProfileIOData : public ProfileIOData {
 public:
  TestProfileIOData(bool is_incognito, PrefService* pref_service,
                    PrefService* local_state, CookieSettings* cookie_settings)
      : ProfileIOData(is_incognito) {
    // Initialize the IO members required for these tests, but keep them on
    // this thread since we don't use a background thread here.
    google_services_username()->Init(prefs::kGoogleServicesUsername,
                                     pref_service, NULL);
    reverse_autologin_enabled()->Init(prefs::kReverseAutologinEnabled,
                                      pref_service, NULL);
    one_click_signin_rejected_email_list()->Init(
        prefs::kReverseAutologinRejectedEmailList, pref_service, NULL);

    google_services_username_pattern()->Init(
        prefs::kGoogleServicesUsernamePattern, local_state, NULL);

    set_signin_names_for_testing(new SigninNamesOnIOThread());
    SetCookieSettingsForTesting(cookie_settings);
  }

  virtual ~TestProfileIOData() {
    signin_names()->ReleaseResourcesOnUIThread();
  }

  // ProfileIOData overrides:
  virtual void LazyInitializeInternal(
      ProfileParams* profile_params) const OVERRIDE {
    NOTREACHED();
  }
  virtual void InitializeExtensionsRequestContext(
      ProfileParams* profile_params) const OVERRIDE {
    NOTREACHED();
  }
  virtual ChromeURLRequestContext* InitializeAppRequestContext(
      ChromeURLRequestContext* main_context,
      const StoragePartitionDescriptor& details,
      scoped_ptr<net::URLRequestJobFactory::Interceptor>
          protocol_handler_interceptor) const OVERRIDE {
    NOTREACHED();
    return NULL;
  }
  virtual ChromeURLRequestContext* InitializeMediaRequestContext(
      ChromeURLRequestContext* original_context,
      const StoragePartitionDescriptor& details) const OVERRIDE {
    NOTREACHED();
    return NULL;
  }
  virtual ChromeURLRequestContext*
      AcquireMediaRequestContext() const OVERRIDE {
    NOTREACHED();
    return NULL;
  }
  virtual ChromeURLRequestContext*
      AcquireIsolatedAppRequestContext(
          ChromeURLRequestContext* main_context,
          const StoragePartitionDescriptor& partition_descriptor,
          scoped_ptr<net::URLRequestJobFactory::Interceptor>
              protocol_handler_interceptor) const OVERRIDE {
    NOTREACHED();
    return NULL;
  }
  virtual ChromeURLRequestContext*
      AcquireIsolatedMediaRequestContext(
          ChromeURLRequestContext* app_context,
          const StoragePartitionDescriptor& partition_descriptor)
          const OVERRIDE {
    NOTREACHED();
    return NULL;
  }
  virtual chrome_browser_net::LoadTimeStats* GetLoadTimeStats(
      IOThread::Globals* io_thread_globals) const OVERRIDE {
    NOTREACHED();
    return NULL;
  }
};

class TestURLRequest : public base::SupportsUserData {
public:
  TestURLRequest() {}
  virtual ~TestURLRequest() {}
};

class OneClickTestProfileSyncService : public TestProfileSyncService {
  public:
   virtual ~OneClickTestProfileSyncService() {}

   // Helper routine to be used in conjunction with
   // ProfileKeyedServiceFactory::SetTestingFactory().
   static ProfileKeyedService* Build(Profile* profile) {
     return new OneClickTestProfileSyncService(profile);
   }

   // Need to control this for certain tests.
   virtual bool FirstSetupInProgress() const OVERRIDE {
     return first_setup_in_progress_;
   }

   // Controls return value of FirstSetupInProgress. Because some bits
   // of UI depend on that value, it's useful to control it separately
   // from the internal work and components that are triggered (such as
   // ReconfigureDataTypeManager) to facilitate unit tests.
   void set_first_setup_in_progress(bool in_progress) {
     first_setup_in_progress_ = in_progress;
   }

   // Override ProfileSyncService::Shutdown() to avoid CHECK on
   // |invalidator_registrar_|.
   void Shutdown() OVERRIDE {};

  private:
   explicit OneClickTestProfileSyncService(Profile* profile)
       : TestProfileSyncService(NULL,
                                profile,
                                NULL,
                                ProfileSyncService::MANUAL_START,
                                false,  // synchronous_backend_init
                                base::Closure()),
         first_setup_in_progress_(false) {}

   bool first_setup_in_progress_;
};

static ProfileKeyedService* BuildSigninManagerMock(Profile* profile) {
  return new SigninManagerMock(profile);
}

}  // namespace

class OneClickSigninHelperTest : public content::RenderViewHostTestHarness {
 public:
  OneClickSigninHelperTest();

  virtual void SetUp() OVERRIDE;

  // Creates the sign-in manager for tests.  If |use_incognito| is true then
  // a WebContents for an incognito profile is created.  If |username| is
  // is not empty, the profile of the mock WebContents will be connected to
  // the given account.
  void CreateSigninManager(bool use_incognito, const std::string& username);

  void AddEmailToOneClickRejectedList(const std::string& email);
  void EnableOneClick(bool enable);
  void AllowSigninCookies(bool enable);
  void SetAllowedUsernamePattern(const std::string& pattern);

  SigninManagerMock* signin_manager_;

 private:
  // Members to fake that we are on the UI thread.
  content::TestBrowserThread ui_thread_;

  DISALLOW_COPY_AND_ASSIGN(OneClickSigninHelperTest);
};

OneClickSigninHelperTest::OneClickSigninHelperTest()
    : ui_thread_(content::BrowserThread::UI, &message_loop_) {
}

void OneClickSigninHelperTest::SetUp() {
  // Make sure web flow is enabled for tests.
  CommandLine::ForCurrentProcess()->AppendSwitch(
      switches::kUseWebBasedSigninFlow);

  TestingProfile* testing_profile = new TestingProfile();
  browser_context_.reset(testing_profile);

  content::RenderViewHostTestHarness::SetUp();
}

void OneClickSigninHelperTest::CreateSigninManager(
    bool use_incognito,
    const std::string& username) {
  TestingProfile* testing_profile = static_cast<TestingProfile*>(
      browser_context_.get());
  testing_profile->set_incognito(use_incognito);
  signin_manager_ = static_cast<SigninManagerMock*>(
      SigninManagerFactory::GetInstance()->SetTestingFactoryAndUse(
          testing_profile, BuildSigninManagerMock));

  if (!username.empty()) {
    signin_manager_->StartSignIn(username, std::string(), std::string(),
                                std::string());
  }
}

void OneClickSigninHelperTest::EnableOneClick(bool enable) {
  PrefService* pref_service = Profile::FromBrowserContext(
      browser_context_.get())->GetPrefs();
  pref_service->SetBoolean(prefs::kReverseAutologinEnabled, enable);
}

void OneClickSigninHelperTest::AddEmailToOneClickRejectedList(
    const std::string& email) {
  PrefService* pref_service = Profile::FromBrowserContext(
      browser_context_.get())->GetPrefs();
  ListPrefUpdate updater(pref_service,
                         prefs::kReverseAutologinRejectedEmailList);
  updater->AppendIfNotPresent(new base::StringValue(email));
}

void OneClickSigninHelperTest::AllowSigninCookies(bool enable) {
  CookieSettings* cookie_settings =
      CookieSettings::Factory::GetForProfile(
          Profile::FromBrowserContext(browser_context_.get()));
  cookie_settings->SetDefaultCookieSetting(
      enable ? CONTENT_SETTING_ALLOW : CONTENT_SETTING_BLOCK);
}

void OneClickSigninHelperTest::SetAllowedUsernamePattern(
    const std::string& pattern) {
  PrefService* local_state = g_browser_process->local_state();
  local_state->SetString(prefs::kGoogleServicesUsernamePattern, pattern);
}

class OneClickSigninHelperIOTest : public OneClickSigninHelperTest {
 public:
  OneClickSigninHelperIOTest();
  virtual ~OneClickSigninHelperIOTest();

  virtual void SetUp() OVERRIDE;

  TestProfileIOData* CreateTestProfileIOData(bool is_incognito);

 protected:
  TestingProfileManager testing_profile_manager_;
  TestURLRequest request_;
  const GURL valid_gaia_url_;

 private:
  content::TestBrowserThread db_thread_;
  content::TestBrowserThread fub_thread_;
  content::TestBrowserThread io_thread_;

  DISALLOW_COPY_AND_ASSIGN(OneClickSigninHelperIOTest);
};

OneClickSigninHelperIOTest::OneClickSigninHelperIOTest()
    : testing_profile_manager_(
          static_cast<TestingBrowserProcess*>(g_browser_process)),
      valid_gaia_url_("https://accounts.google.com/"),
      db_thread_(content::BrowserThread::DB, &message_loop_),
      fub_thread_(content::BrowserThread::FILE_USER_BLOCKING, &message_loop_),
      io_thread_(content::BrowserThread::IO, &message_loop_) {
}

OneClickSigninHelperIOTest::~OneClickSigninHelperIOTest() {
}

void OneClickSigninHelperIOTest::SetUp() {
  OneClickSigninHelperTest::SetUp();
  ASSERT_TRUE(testing_profile_manager_.SetUp());
  OneClickSigninHelper::AssociateWithRequestForTesting(&request_,
                                                       "user@gmail.com");
}

TestProfileIOData* OneClickSigninHelperIOTest::CreateTestProfileIOData(
    bool is_incognito) {
  TestingProfile* testing_profile = static_cast<TestingProfile*>(
      browser_context_.get());
  PrefService* pref_service = testing_profile->GetPrefs();
  PrefService* local_state = g_browser_process->local_state();
  CookieSettings* cookie_settings =
      CookieSettings::Factory::GetForProfile(testing_profile);
  TestProfileIOData* io_data = new TestProfileIOData(
      is_incognito, pref_service, local_state, cookie_settings);
  return io_data;
}

TEST_F(OneClickSigninHelperTest, CanOfferNoContents) {
  EXPECT_FALSE(OneClickSigninHelper::CanOffer(NULL, "user@gmail.com", true));
  EXPECT_FALSE(OneClickSigninHelper::CanOffer(NULL, "", false));
}

TEST_F(OneClickSigninHelperTest, CanOffer) {
  CreateSigninManager(false, "");

  EXPECT_CALL(*signin_manager_, IsAllowedUsername(_)).
        WillRepeatedly(Return(true));

  EnableOneClick(true);
  EXPECT_TRUE(OneClickSigninHelper::CanOffer(web_contents(), "user@gmail.com",
                                             true));
  EXPECT_TRUE(OneClickSigninHelper::CanOffer(web_contents(), "", false));

  EnableOneClick(false);
  EXPECT_FALSE(OneClickSigninHelper::CanOffer(web_contents(), "user@gmail.com",
                                              true));
  EXPECT_FALSE(OneClickSigninHelper::CanOffer(web_contents(), "", false));
}

TEST_F(OneClickSigninHelperTest, CanOfferFirstSetup) {
  CreateSigninManager(false, "");

  EXPECT_CALL(*signin_manager_, IsAllowedUsername(_)).
        WillRepeatedly(Return(true));

   // Invoke OneClickTestProfileSyncService factory function and grab result.
   OneClickTestProfileSyncService* sync =
       static_cast<OneClickTestProfileSyncService*>(
           ProfileSyncServiceFactory::GetInstance()->SetTestingFactoryAndUse(
               static_cast<Profile*>(browser_context()),
               OneClickTestProfileSyncService::Build));

   sync->set_first_setup_in_progress(true);

   EXPECT_FALSE(OneClickSigninHelper::CanOffer(web_contents(),
                                               "foo@gmail.com",
                                               true));
   EXPECT_TRUE(OneClickSigninHelper::CanOffer(web_contents(),
                                              "foo@gmail.com",
                                              false));
}

TEST_F(OneClickSigninHelperTest, CanOfferProfileConnected) {
  CreateSigninManager(false, "foo@gmail.com");

  EXPECT_CALL(*signin_manager_, IsAllowedUsername(_)).
      WillRepeatedly(Return(true));

  EXPECT_FALSE(OneClickSigninHelper::CanOffer(web_contents(),
                                              "foo@gmail.com",
                                              true));
  EXPECT_FALSE(OneClickSigninHelper::CanOffer(web_contents(),
                                              "user@gmail.com",
                                              true));
  EXPECT_TRUE(OneClickSigninHelper::CanOffer(web_contents(),
                                             "",
                                             false));
}

TEST_F(OneClickSigninHelperTest, CanOfferUsernameNotAllowed) {
  CreateSigninManager(false, "foo@gmail.com");

  EXPECT_CALL(*signin_manager_, IsAllowedUsername(_)).
      WillRepeatedly(Return(false));

  EXPECT_FALSE(OneClickSigninHelper::CanOffer(web_contents(),
                                              "foo@gmail.com",
                                              true));
  EXPECT_TRUE(OneClickSigninHelper::CanOffer(web_contents(),
                                             "",
                                             false));
}

TEST_F(OneClickSigninHelperTest, CanOfferWithRejectedEmail) {
  CreateSigninManager(false, "");

  EXPECT_CALL(*signin_manager_, IsAllowedUsername(_)).
        WillRepeatedly(Return(true));

  AddEmailToOneClickRejectedList("foo@gmail.com");
  AddEmailToOneClickRejectedList("user@gmail.com");
  EXPECT_FALSE(OneClickSigninHelper::CanOffer(web_contents(), "foo@gmail.com",
                                              true));
  EXPECT_FALSE(OneClickSigninHelper::CanOffer(web_contents(), "user@gmail.com",
                                              true));
  EXPECT_TRUE(OneClickSigninHelper::CanOffer(web_contents(), "john@gmail.com",
                                              true));
}

TEST_F(OneClickSigninHelperTest, CanOfferIncognito) {
  CreateSigninManager(true, "");

  EXPECT_FALSE(OneClickSigninHelper::CanOffer(web_contents(), "user@gmail.com",
                                              true));
  EXPECT_FALSE(OneClickSigninHelper::CanOffer(web_contents(), "", false));
}

TEST_F(OneClickSigninHelperTest, CanOfferNoSigninCookies) {
  CreateSigninManager(false, "");
  AllowSigninCookies(false);

  EXPECT_CALL(*signin_manager_, IsAllowedUsername(_)).
        WillRepeatedly(Return(true));

  EXPECT_FALSE(OneClickSigninHelper::CanOffer(web_contents(), "user@gmail.com",
                                              true));
  EXPECT_FALSE(OneClickSigninHelper::CanOffer(web_contents(), "", false));
}

// I/O thread tests

TEST_F(OneClickSigninHelperIOTest, CanOfferOnIOThread) {
  scoped_ptr<TestProfileIOData> io_data(CreateTestProfileIOData(false));
  EXPECT_EQ(OneClickSigninHelper::CAN_OFFER,
            OneClickSigninHelper::CanOfferOnIOThreadImpl(
                valid_gaia_url_, "", &request_, io_data.get()));
}

TEST_F(OneClickSigninHelperIOTest, CanOfferOnIOThreadIncognito) {
  scoped_ptr<TestProfileIOData> io_data(CreateTestProfileIOData(true));
  EXPECT_EQ(OneClickSigninHelper::DONT_OFFER,
            OneClickSigninHelper::CanOfferOnIOThreadImpl(
                valid_gaia_url_, "", &request_, io_data.get()));
}

TEST_F(OneClickSigninHelperIOTest, CanOfferOnIOThreadNoIOData) {
  EXPECT_EQ(OneClickSigninHelper::DONT_OFFER,
            OneClickSigninHelper::CanOfferOnIOThreadImpl(
                valid_gaia_url_, "", &request_, NULL));
}

TEST_F(OneClickSigninHelperIOTest, CanOfferOnIOThreadBadURL) {
  scoped_ptr<TestProfileIOData> io_data(CreateTestProfileIOData(false));
  EXPECT_EQ(OneClickSigninHelper::IGNORE_REQUEST,
            OneClickSigninHelper::CanOfferOnIOThreadImpl(
                GURL("https://foo.com/"), "", &request_, io_data.get()));
  EXPECT_EQ(OneClickSigninHelper::IGNORE_REQUEST,
            OneClickSigninHelper::CanOfferOnIOThreadImpl(
                GURL("http://accounts.google.com/"), "",
                &request_, io_data.get()));
}

TEST_F(OneClickSigninHelperIOTest, CanOfferOnIOThreadReferrer) {
  scoped_ptr<TestProfileIOData> io_data(CreateTestProfileIOData(false));
  EXPECT_EQ(OneClickSigninHelper::DONT_OFFER,
            OneClickSigninHelper::CanOfferOnIOThreadImpl(
                valid_gaia_url_, kExplicitURLString, &request_, io_data.get()));
  EXPECT_EQ(OneClickSigninHelper::CAN_OFFER,
            OneClickSigninHelper::CanOfferOnIOThreadImpl(
                valid_gaia_url_, kImplicitURLString, &request_, io_data.get()));

  EXPECT_EQ(OneClickSigninHelper::CAN_OFFER,
            OneClickSigninHelper::CanOfferOnIOThreadImpl(
                valid_gaia_url_, kBad1ExplicitURLString, &request_,
                io_data.get()));
  EXPECT_EQ(OneClickSigninHelper::CAN_OFFER,
            OneClickSigninHelper::CanOfferOnIOThreadImpl(
                valid_gaia_url_, kBad2ExplicitURLString, &request_,
                io_data.get()));
  EXPECT_EQ(OneClickSigninHelper::CAN_OFFER,
            OneClickSigninHelper::CanOfferOnIOThreadImpl(
                valid_gaia_url_, kBad3ExplicitURLString, &request_,
                io_data.get()));
}

TEST_F(OneClickSigninHelperIOTest, CanOfferOnIOThreadDisabled) {
  EnableOneClick(false);
  scoped_ptr<TestProfileIOData> io_data(CreateTestProfileIOData(false));
  EXPECT_EQ(OneClickSigninHelper::DONT_OFFER,
            OneClickSigninHelper::CanOfferOnIOThreadImpl(
                valid_gaia_url_, "", &request_, io_data.get()));
}

TEST_F(OneClickSigninHelperIOTest, CanOfferOnIOThreadSignedIn) {
  TestingProfile* testing_profile = static_cast<TestingProfile*>(
      browser_context_.get());
  PrefService* pref_service = testing_profile->GetPrefs();
  pref_service->SetString(prefs::kGoogleServicesUsername, "user@gmail.com");

  scoped_ptr<TestProfileIOData> io_data(CreateTestProfileIOData(false));
  EXPECT_EQ(OneClickSigninHelper::DONT_OFFER,
            OneClickSigninHelper::CanOfferOnIOThreadImpl(
                valid_gaia_url_, "", &request_, io_data.get()));
}

TEST_F(OneClickSigninHelperIOTest, CanOfferOnIOThreadEmailNotAllowed) {
  SetAllowedUsernamePattern("*@example.com");
  scoped_ptr<TestProfileIOData> io_data(CreateTestProfileIOData(false));
  EXPECT_EQ(OneClickSigninHelper::DONT_OFFER,
            OneClickSigninHelper::CanOfferOnIOThreadImpl(
                valid_gaia_url_, "", &request_, io_data.get()));
}

TEST_F(OneClickSigninHelperIOTest, CanOfferOnIOThreadEmailAlreadyUsed) {
  ProfileInfoCache* cache = testing_profile_manager_.profile_info_cache();
  const FilePath& user_data_dir = cache->GetUserDataDir();
  cache->AddProfileToCache(user_data_dir.Append(FILE_PATH_LITERAL("user")),
                           UTF8ToUTF16("user"),
                           UTF8ToUTF16("user@gmail.com"), 0);

  scoped_ptr<TestProfileIOData> io_data(CreateTestProfileIOData(false));
  EXPECT_EQ(OneClickSigninHelper::DONT_OFFER,
            OneClickSigninHelper::CanOfferOnIOThreadImpl(
                valid_gaia_url_, "", &request_, io_data.get()));
}

TEST_F(OneClickSigninHelperIOTest, CanOfferOnIOThreadWithRejectedEmail) {
  AddEmailToOneClickRejectedList("user@gmail.com");
  scoped_ptr<TestProfileIOData> io_data(CreateTestProfileIOData(false));
  EXPECT_EQ(OneClickSigninHelper::DONT_OFFER,
            OneClickSigninHelper::CanOfferOnIOThreadImpl(
                valid_gaia_url_, "", &request_, io_data.get()));
}

TEST_F(OneClickSigninHelperIOTest, CanOfferOnIOThreadNoSigninCookies) {
  AllowSigninCookies(false);
  scoped_ptr<TestProfileIOData> io_data(CreateTestProfileIOData(false));
  EXPECT_EQ(OneClickSigninHelper::DONT_OFFER,
            OneClickSigninHelper::CanOfferOnIOThreadImpl(
                valid_gaia_url_, "", &request_, io_data.get()));
}
