// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/prefs/public/pref_change_registrar.h"
#include "base/prefs/public/pref_observer.h"
#include "chrome/test/base/testing_pref_service.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::Mock;
using testing::Eq;

namespace {

const char kHomePage[] = "homepage";
const char kHomePageIsNewTabPage[] = "homepage_is_newtabpage";
const char kApplicationLocale[] = "intl.app_locale";

// TODO(joi): Use PrefObserverMock once it moves to base/prefs/.
class MockPrefObserver : public PrefObserver {
 public:
  virtual ~MockPrefObserver() {}

  MOCK_METHOD2(OnPreferenceChanged, void(PrefServiceBase*, const std::string&));
};

// A mock provider that allows us to capture pref observer changes.
class MockPrefService : public TestingPrefService {
 public:
  MockPrefService() {}
  virtual ~MockPrefService() {}

  MOCK_METHOD2(AddPrefObserver,
               void(const char*, PrefObserver*));
  MOCK_METHOD2(RemovePrefObserver,
               void(const char*, PrefObserver*));
};

}  // namespace

class PrefChangeRegistrarTest : public testing::Test {
 public:
  PrefChangeRegistrarTest() {}
  virtual ~PrefChangeRegistrarTest() {}

 protected:
  virtual void SetUp();

  PrefObserver* observer() const { return observer_.get(); }
  MockPrefService* service() const { return service_.get(); }

 private:
  scoped_ptr<MockPrefService> service_;
  scoped_ptr<MockPrefObserver> observer_;
};

void PrefChangeRegistrarTest::SetUp() {
  service_.reset(new MockPrefService());
  observer_.reset(new MockPrefObserver());
}

TEST_F(PrefChangeRegistrarTest, AddAndRemove) {
  PrefChangeRegistrar registrar;
  registrar.Init(service());

  // Test adding.
  EXPECT_CALL(*service(),
              AddPrefObserver(Eq(std::string("test.pref.1")), &registrar));
  EXPECT_CALL(*service(),
              AddPrefObserver(Eq(std::string("test.pref.2")), &registrar));
  registrar.Add("test.pref.1", observer());
  registrar.Add("test.pref.2", observer());
  EXPECT_FALSE(registrar.IsEmpty());

  // Test removing.
  Mock::VerifyAndClearExpectations(service());
  EXPECT_CALL(*service(),
              RemovePrefObserver(Eq(std::string("test.pref.1")), &registrar));
  EXPECT_CALL(*service(),
              RemovePrefObserver(Eq(std::string("test.pref.2")), &registrar));
  registrar.Remove("test.pref.1");
  registrar.Remove("test.pref.2");
  EXPECT_TRUE(registrar.IsEmpty());

  // Explicitly check the expectations now to make sure that the Removes
  // worked (rather than the registrar destructor doing the work).
  Mock::VerifyAndClearExpectations(service());
}

TEST_F(PrefChangeRegistrarTest, AutoRemove) {
  PrefChangeRegistrar registrar;
  registrar.Init(service());

  // Setup of auto-remove.
  EXPECT_CALL(*service(),
              AddPrefObserver(Eq(std::string("test.pref.1")), &registrar));
  registrar.Add("test.pref.1", observer());
  Mock::VerifyAndClearExpectations(service());
  EXPECT_FALSE(registrar.IsEmpty());

  // Test auto-removing.
  EXPECT_CALL(*service(),
              RemovePrefObserver(Eq(std::string("test.pref.1")), &registrar));
}

TEST_F(PrefChangeRegistrarTest, RemoveAll) {
  PrefChangeRegistrar registrar;
  registrar.Init(service());

  EXPECT_CALL(*service(),
              AddPrefObserver(Eq(std::string("test.pref.1")), &registrar));
  EXPECT_CALL(*service(),
              AddPrefObserver(Eq(std::string("test.pref.2")), &registrar));
  registrar.Add("test.pref.1", observer());
  registrar.Add("test.pref.2", observer());
  Mock::VerifyAndClearExpectations(service());

  EXPECT_CALL(*service(),
              RemovePrefObserver(Eq(std::string("test.pref.1")), &registrar));
  EXPECT_CALL(*service(),
              RemovePrefObserver(Eq(std::string("test.pref.2")), &registrar));
  registrar.RemoveAll();
  EXPECT_TRUE(registrar.IsEmpty());

  // Explicitly check the expectations now to make sure that the RemoveAll
  // worked (rather than the registrar destructor doing the work).
  Mock::VerifyAndClearExpectations(service());
}

class ObserveSetOfPreferencesTest : public testing::Test,
                                    public PrefObserver {
 public:
  virtual void SetUp() {
    pref_service_.reset(new TestingPrefService);
    pref_service_->RegisterStringPref(kHomePage,
                                      "http://google.com",
                                      PrefService::UNSYNCABLE_PREF);
    pref_service_->RegisterBooleanPref(kHomePageIsNewTabPage,
                                       false,
                                       PrefService::UNSYNCABLE_PREF);
    pref_service_->RegisterStringPref(kApplicationLocale,
                                      "",
                                      PrefService::UNSYNCABLE_PREF);
  }

  PrefChangeRegistrar* CreatePrefChangeRegistrar(
      PrefObserver* observer) {
    if (!observer)
      observer = this;
    PrefChangeRegistrar* pref_set = new PrefChangeRegistrar();
    pref_set->Init(pref_service_.get());
    pref_set->Add(kHomePage, observer);
    pref_set->Add(kHomePageIsNewTabPage, observer);
    return pref_set;
  }

  // PrefObserver (used to enable NULL as parameter to
  // CreatePrefChangeRegistrar above).
  virtual void OnPreferenceChanged(PrefServiceBase* service,
                                   const std::string& pref_name) OVERRIDE {
  }

  scoped_ptr<TestingPrefService> pref_service_;
};

TEST_F(ObserveSetOfPreferencesTest, IsObserved) {
  scoped_ptr<PrefChangeRegistrar> pref_set(CreatePrefChangeRegistrar(NULL));
  EXPECT_TRUE(pref_set->IsObserved(kHomePage));
  EXPECT_TRUE(pref_set->IsObserved(kHomePageIsNewTabPage));
  EXPECT_FALSE(pref_set->IsObserved(kApplicationLocale));
}

TEST_F(ObserveSetOfPreferencesTest, IsManaged) {
  scoped_ptr<PrefChangeRegistrar> pref_set(CreatePrefChangeRegistrar(NULL));
  EXPECT_FALSE(pref_set->IsManaged());
  pref_service_->SetManagedPref(kHomePage,
                                Value::CreateStringValue("http://crbug.com"));
  EXPECT_TRUE(pref_set->IsManaged());
  pref_service_->SetManagedPref(kHomePageIsNewTabPage,
                                Value::CreateBooleanValue(true));
  EXPECT_TRUE(pref_set->IsManaged());
  pref_service_->RemoveManagedPref(kHomePage);
  EXPECT_TRUE(pref_set->IsManaged());
  pref_service_->RemoveManagedPref(kHomePageIsNewTabPage);
  EXPECT_FALSE(pref_set->IsManaged());
}

TEST_F(ObserveSetOfPreferencesTest, Observe) {
  using testing::_;
  using testing::Mock;

  MockPrefObserver observer;
  scoped_ptr<PrefChangeRegistrar> pref_set(
      CreatePrefChangeRegistrar(&observer));

  EXPECT_CALL(observer, OnPreferenceChanged(pref_service_.get(), kHomePage));
  pref_service_->SetUserPref(kHomePage,
                             Value::CreateStringValue("http://crbug.com"));
  Mock::VerifyAndClearExpectations(&observer);

  EXPECT_CALL(observer, OnPreferenceChanged(pref_service_.get(),
                                            kHomePageIsNewTabPage));
  pref_service_->SetUserPref(kHomePageIsNewTabPage,
                             Value::CreateBooleanValue(true));
  Mock::VerifyAndClearExpectations(&observer);

  EXPECT_CALL(observer, OnPreferenceChanged(_, _)).Times(0);
  pref_service_->SetUserPref(kApplicationLocale,
                             Value::CreateStringValue("en_US.utf8"));
  Mock::VerifyAndClearExpectations(&observer);
}
