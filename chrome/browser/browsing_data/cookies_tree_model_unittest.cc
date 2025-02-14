// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/browsing_data/cookies_tree_model.h"

#include <string>

#include "base/message_loop.h"
#include "chrome/browser/browsing_data/mock_browsing_data_appcache_helper.h"
#include "chrome/browser/browsing_data/mock_browsing_data_cookie_helper.h"
#include "chrome/browser/browsing_data/mock_browsing_data_database_helper.h"
#include "chrome/browser/browsing_data/mock_browsing_data_file_system_helper.h"
#include "chrome/browser/browsing_data/mock_browsing_data_flash_lso_helper.h"
#include "chrome/browser/browsing_data/mock_browsing_data_indexed_db_helper.h"
#include "chrome/browser/browsing_data/mock_browsing_data_local_storage_helper.h"
#include "chrome/browser/browsing_data/mock_browsing_data_quota_helper.h"
#include "chrome/browser/browsing_data/mock_browsing_data_server_bound_cert_helper.h"
#include "chrome/browser/content_settings/cookie_settings.h"
#include "chrome/browser/content_settings/host_content_settings_map.h"
#include "chrome/browser/content_settings/mock_settings_observer.h"
#include "chrome/browser/extensions/extension_special_storage_policy.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_types.h"
#include "content/public/test/test_browser_thread.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_getter.h"
#include "testing/gtest/include/gtest/gtest.h"

#include "base/utf_string_conversions.h"

using ::testing::_;
using content::BrowserThread;

namespace {

class CookiesTreeModelTest : public testing::Test {
 public:
  CookiesTreeModelTest()
      : ui_thread_(BrowserThread::UI, &message_loop_),
        file_user_blocking_(BrowserThread::FILE_USER_BLOCKING, &message_loop_),
        io_thread_(BrowserThread::IO, &message_loop_) {
  }

  virtual ~CookiesTreeModelTest() {
    // Avoid memory leaks.
    special_storage_policy_ = NULL;
    profile_.reset();
    message_loop_.RunUntilIdle();
  }

  virtual void SetUp() OVERRIDE {
    profile_.reset(new TestingProfile());
    profile_->CreateRequestContext();
    mock_browsing_data_cookie_helper_ =
        new MockBrowsingDataCookieHelper(profile_->GetRequestContext());
    mock_browsing_data_database_helper_ =
        new MockBrowsingDataDatabaseHelper(profile_.get());
    mock_browsing_data_local_storage_helper_ =
        new MockBrowsingDataLocalStorageHelper(profile_.get());
    mock_browsing_data_session_storage_helper_ =
        new MockBrowsingDataLocalStorageHelper(profile_.get());
    mock_browsing_data_appcache_helper_ =
        new MockBrowsingDataAppCacheHelper(profile_.get());
    mock_browsing_data_indexed_db_helper_ =
        new MockBrowsingDataIndexedDBHelper();
    mock_browsing_data_file_system_helper_ =
        new MockBrowsingDataFileSystemHelper(profile_.get());
    mock_browsing_data_quota_helper_ =
        new MockBrowsingDataQuotaHelper(profile_.get());
    mock_browsing_data_server_bound_cert_helper_ =
        new MockBrowsingDataServerBoundCertHelper();
    mock_browsing_data_flash_lso_helper_ =
        new MockBrowsingDataFlashLSOHelper(profile_.get());

    // It is fine to reuse the profile request context for the app, since
    // the mock cookie helper maintains its own list internally and doesn't
    // really use the request context. Same is true for the rest.
    mock_browsing_data_cookie_helper_app_ =
        new MockBrowsingDataCookieHelper(profile_->GetRequestContext());

    scoped_refptr<CookieSettings> cookie_settings =
        new CookieSettings(profile_->GetHostContentSettingsMap(),
                           profile_->GetPrefs());
    special_storage_policy_ =
        new ExtensionSpecialStoragePolicy(cookie_settings);
  }

  virtual void TearDown() OVERRIDE {
    mock_browsing_data_server_bound_cert_helper_ = NULL;
    mock_browsing_data_quota_helper_ = NULL;
    mock_browsing_data_file_system_helper_ = NULL;
    mock_browsing_data_indexed_db_helper_ = NULL;
    mock_browsing_data_appcache_helper_ = NULL;
    mock_browsing_data_session_storage_helper_ = NULL;
    mock_browsing_data_local_storage_helper_ = NULL;
    mock_browsing_data_database_helper_ = NULL;
    mock_browsing_data_flash_lso_helper_ = NULL;
    message_loop_.RunUntilIdle();
  }

  scoped_ptr<CookiesTreeModel> CreateCookiesTreeModelWithInitialSample(
      bool add_app) {
    ContainerMap containers_map;

    containers_map[std::string()] = new LocalDataContainer(
        "Drive-By-Web", std::string(),
        mock_browsing_data_cookie_helper_,
        mock_browsing_data_database_helper_,
        mock_browsing_data_local_storage_helper_,
        mock_browsing_data_session_storage_helper_,
        mock_browsing_data_appcache_helper_,
        mock_browsing_data_indexed_db_helper_,
        mock_browsing_data_file_system_helper_,
        mock_browsing_data_quota_helper_,
        mock_browsing_data_server_bound_cert_helper_,
        mock_browsing_data_flash_lso_helper_);

    if (add_app) {
      std::string app_id = "some-random-id";
      // The cookie helper is mandatory, the rest can be NULL.
      containers_map[app_id] = new LocalDataContainer(
          "Isolated App", app_id,
          mock_browsing_data_cookie_helper_app_,
          NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    }

    CookiesTreeModel* cookies_model =
        new CookiesTreeModel(containers_map,
                             special_storage_policy_,
                             false);
    mock_browsing_data_cookie_helper_->
        AddCookieSamples(GURL("http://foo1"), "A=1");
    mock_browsing_data_cookie_helper_->
        AddCookieSamples(GURL("http://foo2"), "B=1");
    mock_browsing_data_cookie_helper_->
        AddCookieSamples(GURL("http://foo3"), "C=1");
    mock_browsing_data_cookie_helper_->Notify();
    mock_browsing_data_database_helper_->AddDatabaseSamples();
    mock_browsing_data_database_helper_->Notify();
    mock_browsing_data_local_storage_helper_->AddLocalStorageSamples();
    mock_browsing_data_local_storage_helper_->Notify();
    mock_browsing_data_session_storage_helper_->AddLocalStorageSamples();
    mock_browsing_data_session_storage_helper_->Notify();
    mock_browsing_data_indexed_db_helper_->AddIndexedDBSamples();
    mock_browsing_data_indexed_db_helper_->Notify();
    mock_browsing_data_file_system_helper_->AddFileSystemSamples();
    mock_browsing_data_file_system_helper_->Notify();
    mock_browsing_data_quota_helper_->AddQuotaSamples();
    mock_browsing_data_quota_helper_->Notify();
    mock_browsing_data_server_bound_cert_helper_->AddServerBoundCertSample(
        "sbc1");
    mock_browsing_data_server_bound_cert_helper_->AddServerBoundCertSample(
        "sbc2");
    mock_browsing_data_server_bound_cert_helper_->Notify();
    mock_browsing_data_flash_lso_helper_->AddFlashLSODomain("xyz.com");
    mock_browsing_data_flash_lso_helper_->Notify();

    if (add_app) {
      mock_browsing_data_cookie_helper_app_->
          AddCookieSamples(GURL("http://app-origin1"), "Z=1");
      mock_browsing_data_cookie_helper_app_->
          AddCookieSamples(GURL("http://app-origin2"), "Y=1");
      mock_browsing_data_cookie_helper_app_->
          AddCookieSamples(GURL("http://app-origin3"), "X=1");
      mock_browsing_data_cookie_helper_app_->Notify();
    }

    {
      SCOPED_TRACE("Initial State 3 cookies, 2 databases, 2 local storages, "
                   "2 session storages, 2 indexed DBs, 3 filesystems, "
                   "2 quotas, 2 server bound certs, 1 Flash LSO");
      // 51 because there's the root, then
      // foo1 -> cookies -> a,
      // foo2 -> cookies -> b,
      // foo3 -> cookies -> c,
      // dbhost1 -> database -> db1,
      // dbhost2 -> database -> db2,
      // host1 -> localstorage -> http://host1:1/,
      //       -> sessionstorage -> http://host1:1/,
      // host2 -> localstorage -> http://host2:2/.
      //       -> sessionstorage -> http://host2:2/,
      // idbhost1 -> indexeddb -> http://idbhost1:1/,
      // idbhost2 -> indexeddb -> http://idbhost2:2/,
      // fshost1 -> filesystem -> http://fshost1:1/,
      // fshost2 -> filesystem -> http://fshost2:1/,
      // fshost3 -> filesystem -> http://fshost3:1/,
      // quotahost1 -> quotahost1,
      // quotahost2 -> quotahost2,
      // sbc1 -> sbcerts -> sbc1,
      // sbc2 -> sbcerts -> sbc2.
      // xyz.com -> flash_lsos
      if (!add_app) {
        EXPECT_EQ(53, cookies_model->GetRoot()->GetTotalNodeCount());
        EXPECT_EQ("A,B,C", GetDisplayedCookies(cookies_model));
      } else {
        // Once we add the app, we have 9 more nodes:
        // app-origin1 -> cookies -> z,
        // app-origin2 -> cookies -> y,
        // app-origin3 -> cookies -> x,
        EXPECT_EQ(62, cookies_model->GetRoot()->GetTotalNodeCount());
        EXPECT_EQ("A,B,C,Z,Y,X", GetDisplayedCookies(cookies_model));
      }
      EXPECT_EQ("db1,db2", GetDisplayedDatabases(cookies_model));
      EXPECT_EQ("http://host1:1/,http://host2:2/",
                GetDisplayedLocalStorages(cookies_model));
      EXPECT_EQ("http://host1:1/,http://host2:2/",
                GetDisplayedSessionStorages(cookies_model));
      EXPECT_EQ("http://idbhost1:1/,http://idbhost2:2/",
                GetDisplayedIndexedDBs(cookies_model));
      EXPECT_EQ("http://fshost1:1/,http://fshost2:2/,http://fshost3:3/",
                GetDisplayedFileSystems(cookies_model));
      EXPECT_EQ("quotahost1,quotahost2",
                GetDisplayedQuotas(cookies_model));
      EXPECT_EQ("sbc1,sbc2",
                GetDisplayedServerBoundCerts(cookies_model));
      EXPECT_EQ("xyz.com",
                GetDisplayedFlashLSOs(cookies_model));
    }
    return make_scoped_ptr(cookies_model);
  }

  std::string GetNodesOfChildren(
      const CookieTreeNode* node,
      CookieTreeNode::DetailedInfo::NodeType node_type) {
    if (!node->empty()) {
      std::string retval;
      for (int i = 0; i < node->child_count(); ++i) {
        retval += GetNodesOfChildren(node->GetChild(i), node_type);
      }
      return retval;
    }

    if (node->GetDetailedInfo().node_type != node_type)
      return std::string();

    switch (node_type) {
      case CookieTreeNode::DetailedInfo::TYPE_SESSION_STORAGE:
        return node->GetDetailedInfo().
            session_storage_info->origin_url.spec() + ",";
      case CookieTreeNode::DetailedInfo::TYPE_LOCAL_STORAGE:
        return node->GetDetailedInfo().
            local_storage_info->origin_url.spec() + ",";
      case CookieTreeNode::DetailedInfo::TYPE_DATABASE:
        return node->GetDetailedInfo().database_info->database_name + ",";
      case CookieTreeNode::DetailedInfo::TYPE_COOKIE:
        return node->GetDetailedInfo().cookie->Name() + ",";
      case CookieTreeNode::DetailedInfo::TYPE_APPCACHE:
        return node->GetDetailedInfo().appcache_info->manifest_url.spec() +
               ",";
      case CookieTreeNode::DetailedInfo::TYPE_INDEXED_DB:
        return node->GetDetailedInfo().indexed_db_info->origin.spec() +
               ",";
      case CookieTreeNode::DetailedInfo::TYPE_FILE_SYSTEM:
        return node->GetDetailedInfo().file_system_info->origin.spec() +
               ",";
      case CookieTreeNode::DetailedInfo::TYPE_QUOTA:
        return node->GetDetailedInfo().quota_info->host + ",";
      case CookieTreeNode::DetailedInfo::TYPE_SERVER_BOUND_CERT:
        return node->GetDetailedInfo(
            ).server_bound_cert->server_identifier() + ",";
      case CookieTreeNode::DetailedInfo::TYPE_FLASH_LSO:
        return node->GetDetailedInfo().flash_lso_domain + ",";
      default:
        return std::string();
    }
  }

  std::string GetCookiesOfChildren(const CookieTreeNode* node) {
    return GetNodesOfChildren(node, CookieTreeNode::DetailedInfo::TYPE_COOKIE);
  }

  std::string GetDatabasesOfChildren(const CookieTreeNode* node) {
    return GetNodesOfChildren(node,
                              CookieTreeNode::DetailedInfo::TYPE_DATABASE);
  }

  std::string GetLocalStoragesOfChildren(const CookieTreeNode* node) {
    return GetNodesOfChildren(node,
                              CookieTreeNode::DetailedInfo::TYPE_LOCAL_STORAGE);
  }

  std::string GetSessionStoragesOfChildren(const CookieTreeNode* node) {
    return GetNodesOfChildren(
        node, CookieTreeNode::DetailedInfo::TYPE_SESSION_STORAGE);
  }

  std::string GetIndexedDBsOfChildren(const CookieTreeNode* node) {
    return GetNodesOfChildren(
        node, CookieTreeNode::DetailedInfo::TYPE_INDEXED_DB);
  }

  std::string GetFileSystemsOfChildren(const CookieTreeNode* node) {
    return GetNodesOfChildren(
        node, CookieTreeNode::DetailedInfo::TYPE_FILE_SYSTEM);
  }

  std::string GetFileQuotaOfChildren(const CookieTreeNode* node) {
    return GetNodesOfChildren(
        node, CookieTreeNode::DetailedInfo::TYPE_QUOTA);
  }

  std::string GetFlashLSOsOfChildren(const CookieTreeNode* node) {
    return GetNodesOfChildren(
        node, CookieTreeNode::DetailedInfo::TYPE_FLASH_LSO);
  }

  // Get the nodes names displayed in the view (if we had one) in the order
  // they are displayed, as a comma seperated string.
  // Ex: EXPECT_STREQ("X,Y", GetDisplayedNodes(cookies_view, type).c_str());
  std::string GetDisplayedNodes(CookiesTreeModel* cookies_model,
                                CookieTreeNode::DetailedInfo::NodeType type) {
    CookieTreeRootNode* root = static_cast<CookieTreeRootNode*>(
        cookies_model->GetRoot());
    std::string retval = GetNodesOfChildren(root, type);
    if (retval.length() && retval[retval.length() - 1] == ',')
      retval.erase(retval.length() - 1);
    return retval;
  }

  std::string GetDisplayedCookies(CookiesTreeModel* cookies_model) {
    return GetDisplayedNodes(cookies_model,
                             CookieTreeNode::DetailedInfo::TYPE_COOKIE);
  }

  std::string GetDisplayedDatabases(CookiesTreeModel* cookies_model) {
    return GetDisplayedNodes(cookies_model,
                             CookieTreeNode::DetailedInfo::TYPE_DATABASE);
  }

  std::string GetDisplayedLocalStorages(CookiesTreeModel* cookies_model) {
    return GetDisplayedNodes(cookies_model,
                             CookieTreeNode::DetailedInfo::TYPE_LOCAL_STORAGE);
  }

  std::string GetDisplayedSessionStorages(CookiesTreeModel* cookies_model) {
    return GetDisplayedNodes(
        cookies_model, CookieTreeNode::DetailedInfo::TYPE_SESSION_STORAGE);
  }

  std::string GetDisplayedAppCaches(CookiesTreeModel* cookies_model) {
    return GetDisplayedNodes(cookies_model,
                             CookieTreeNode::DetailedInfo::TYPE_APPCACHE);
  }

  std::string GetDisplayedIndexedDBs(CookiesTreeModel* cookies_model) {
    return GetDisplayedNodes(cookies_model,
                             CookieTreeNode::DetailedInfo::TYPE_INDEXED_DB);
  }

  std::string GetDisplayedFileSystems(CookiesTreeModel* cookies_model) {
    return GetDisplayedNodes(cookies_model,
                             CookieTreeNode::DetailedInfo::TYPE_FILE_SYSTEM);
  }

  std::string GetDisplayedQuotas(CookiesTreeModel* cookies_model) {
    return GetDisplayedNodes(cookies_model,
                             CookieTreeNode::DetailedInfo::TYPE_QUOTA);
  }

  std::string GetDisplayedServerBoundCerts(CookiesTreeModel* cookies_model) {
    return GetDisplayedNodes(
        cookies_model, CookieTreeNode::DetailedInfo::TYPE_SERVER_BOUND_CERT);
  }

  std::string GetDisplayedFlashLSOs(CookiesTreeModel* cookies_model) {
    return GetDisplayedNodes(
        cookies_model, CookieTreeNode::DetailedInfo::TYPE_FLASH_LSO);
  }

  // Do not call on the root.
  void DeleteStoredObjects(CookieTreeNode* node) {
    node->DeleteStoredObjects();
    CookieTreeNode* parent_node = node->parent();
    DCHECK(parent_node);
    delete parent_node->GetModel()->Remove(parent_node, node);
  }

 protected:
  MessageLoop message_loop_;
  content::TestBrowserThread ui_thread_;
  content::TestBrowserThread file_user_blocking_;
  content::TestBrowserThread io_thread_;

  scoped_ptr<TestingProfile> profile_;
  scoped_refptr<MockBrowsingDataCookieHelper>
      mock_browsing_data_cookie_helper_;
  scoped_refptr<MockBrowsingDataDatabaseHelper>
      mock_browsing_data_database_helper_;
  scoped_refptr<MockBrowsingDataLocalStorageHelper>
      mock_browsing_data_local_storage_helper_;
  scoped_refptr<MockBrowsingDataLocalStorageHelper>
      mock_browsing_data_session_storage_helper_;
  scoped_refptr<MockBrowsingDataAppCacheHelper>
      mock_browsing_data_appcache_helper_;
  scoped_refptr<MockBrowsingDataIndexedDBHelper>
      mock_browsing_data_indexed_db_helper_;
  scoped_refptr<MockBrowsingDataFileSystemHelper>
      mock_browsing_data_file_system_helper_;
  scoped_refptr<MockBrowsingDataQuotaHelper>
      mock_browsing_data_quota_helper_;
  scoped_refptr<MockBrowsingDataServerBoundCertHelper>
      mock_browsing_data_server_bound_cert_helper_;
  scoped_refptr<MockBrowsingDataFlashLSOHelper>
      mock_browsing_data_flash_lso_helper_;

  // App helpers.
  scoped_refptr<MockBrowsingDataCookieHelper>
      mock_browsing_data_cookie_helper_app_;

  scoped_refptr<ExtensionSpecialStoragePolicy> special_storage_policy_;
};

TEST_F(CookiesTreeModelTest, RemoveAll) {
  scoped_ptr<CookiesTreeModel> cookies_model(
      CreateCookiesTreeModelWithInitialSample(false));

  // Reset the selection of the first row.
  {
    SCOPED_TRACE("Before removing");
    EXPECT_EQ("A,B,C",
              GetDisplayedCookies(cookies_model.get()));
    EXPECT_EQ("db1,db2",
              GetDisplayedDatabases(cookies_model.get()));
    EXPECT_EQ("http://host1:1/,http://host2:2/",
              GetDisplayedLocalStorages(cookies_model.get()));
    EXPECT_EQ("http://host1:1/,http://host2:2/",
              GetDisplayedSessionStorages(cookies_model.get()));
    EXPECT_EQ("http://idbhost1:1/,http://idbhost2:2/",
              GetDisplayedIndexedDBs(cookies_model.get()));
    EXPECT_EQ("http://fshost1:1/,http://fshost2:2/,http://fshost3:3/",
              GetDisplayedFileSystems(cookies_model.get()));
    EXPECT_EQ("quotahost1,quotahost2",
              GetDisplayedQuotas(cookies_model.get()));
    EXPECT_EQ("sbc1,sbc2",
              GetDisplayedServerBoundCerts(cookies_model.get()));
    EXPECT_EQ("xyz.com",
              GetDisplayedFlashLSOs(cookies_model.get()));
  }

  mock_browsing_data_cookie_helper_->Reset();
  mock_browsing_data_database_helper_->Reset();
  mock_browsing_data_local_storage_helper_->Reset();
  mock_browsing_data_session_storage_helper_->Reset();
  mock_browsing_data_indexed_db_helper_->Reset();
  mock_browsing_data_file_system_helper_->Reset();

  cookies_model->DeleteAllStoredObjects();

  // Make sure the nodes are also deleted from the model's cache.
  // http://crbug.com/43249
  cookies_model->UpdateSearchResults(string16());

  {
    // 2 nodes - root and app
    SCOPED_TRACE("After removing");
    EXPECT_EQ(1, cookies_model->GetRoot()->GetTotalNodeCount());
    EXPECT_EQ(0, cookies_model->GetRoot()->child_count());
    EXPECT_EQ(std::string(""), GetDisplayedCookies(cookies_model.get()));
    EXPECT_TRUE(mock_browsing_data_cookie_helper_->AllDeleted());
    EXPECT_TRUE(mock_browsing_data_database_helper_->AllDeleted());
    EXPECT_TRUE(mock_browsing_data_local_storage_helper_->AllDeleted());
    EXPECT_FALSE(mock_browsing_data_session_storage_helper_->AllDeleted());
    EXPECT_TRUE(mock_browsing_data_indexed_db_helper_->AllDeleted());
    EXPECT_TRUE(mock_browsing_data_file_system_helper_->AllDeleted());
    EXPECT_TRUE(mock_browsing_data_server_bound_cert_helper_->AllDeleted());
    EXPECT_TRUE(mock_browsing_data_flash_lso_helper_->AllDeleted());
  }
}

TEST_F(CookiesTreeModelTest, Remove) {
  scoped_ptr<CookiesTreeModel> cookies_model(
      CreateCookiesTreeModelWithInitialSample(false));

  // Children start out arranged as follows:
  //
  // 0. `foo1`
  // 1. `foo2`
  // 2. `foo3`
  // 3. `fshost1`
  // 4. `fshost2`
  // 5. `fshost3`
  // 6. `gdbhost1`
  // 7. `gdbhost2`
  // 8. `host1`
  // 9. `host2`
  // 10. `idbhost1`
  // 11. `idbhost2`
  // 12. `quotahost1`
  // 13. `quotahost2`
  // 14. `sbc1`
  // 15. `sbc2`
  // 16. `xyz.com`
  //
  // Here, we'll remove them one by one, starting from the end, and
  // check that the state makes sense.

  DeleteStoredObjects(cookies_model->GetRoot()->GetChild(16));
  {
    SCOPED_TRACE("`xyz.com` removed.");
    EXPECT_STREQ("A,B,C", GetDisplayedCookies(cookies_model.get()).c_str());
    EXPECT_EQ("db1,db2", GetDisplayedDatabases(cookies_model.get()));
    EXPECT_EQ("http://host1:1/,http://host2:2/",
              GetDisplayedLocalStorages(cookies_model.get()));
    EXPECT_EQ("http://host1:1/,http://host2:2/",
              GetDisplayedSessionStorages(cookies_model.get()));
    EXPECT_EQ("http://fshost1:1/,http://fshost2:2/,http://fshost3:3/",
              GetDisplayedFileSystems(cookies_model.get()));
    EXPECT_EQ("http://idbhost1:1/,http://idbhost2:2/",
              GetDisplayedIndexedDBs(cookies_model.get()));
    EXPECT_EQ("quotahost1,quotahost2",
              GetDisplayedQuotas(cookies_model.get()));
    EXPECT_EQ("sbc1,sbc2",
              GetDisplayedServerBoundCerts(cookies_model.get()));
    EXPECT_EQ(51, cookies_model->GetRoot()->GetTotalNodeCount());
  }
  DeleteStoredObjects(cookies_model->GetRoot()->GetChild(15));
  {
    SCOPED_TRACE("`sbc2` removed.");
    EXPECT_STREQ("A,B,C", GetDisplayedCookies(cookies_model.get()).c_str());
    EXPECT_EQ("db1,db2", GetDisplayedDatabases(cookies_model.get()));
    EXPECT_EQ("http://host1:1/,http://host2:2/",
              GetDisplayedLocalStorages(cookies_model.get()));
    EXPECT_EQ("http://host1:1/,http://host2:2/",
              GetDisplayedSessionStorages(cookies_model.get()));
    EXPECT_EQ("http://fshost1:1/,http://fshost2:2/,http://fshost3:3/",
              GetDisplayedFileSystems(cookies_model.get()));
    EXPECT_EQ("http://idbhost1:1/,http://idbhost2:2/",
              GetDisplayedIndexedDBs(cookies_model.get()));
    EXPECT_EQ("quotahost1,quotahost2",
              GetDisplayedQuotas(cookies_model.get()));
    EXPECT_EQ("sbc1",
              GetDisplayedServerBoundCerts(cookies_model.get()));
    EXPECT_EQ(48, cookies_model->GetRoot()->GetTotalNodeCount());
  }
  DeleteStoredObjects(cookies_model->GetRoot()->GetChild(14));
  {
    SCOPED_TRACE("`sbc1` removed.");
    EXPECT_STREQ("A,B,C", GetDisplayedCookies(cookies_model.get()).c_str());
    EXPECT_EQ("db1,db2", GetDisplayedDatabases(cookies_model.get()));
    EXPECT_EQ("http://host1:1/,http://host2:2/",
              GetDisplayedLocalStorages(cookies_model.get()));
    EXPECT_EQ("http://host1:1/,http://host2:2/",
              GetDisplayedSessionStorages(cookies_model.get()));
    EXPECT_EQ("http://fshost1:1/,http://fshost2:2/,http://fshost3:3/",
              GetDisplayedFileSystems(cookies_model.get()));
    EXPECT_EQ("http://idbhost1:1/,http://idbhost2:2/",
              GetDisplayedIndexedDBs(cookies_model.get()));
    EXPECT_EQ("quotahost1,quotahost2",
              GetDisplayedQuotas(cookies_model.get()));
    EXPECT_EQ(45, cookies_model->GetRoot()->GetTotalNodeCount());
  }
  DeleteStoredObjects(cookies_model->GetRoot()->GetChild(13));
  {
    SCOPED_TRACE("`quotahost2` removed.");
    EXPECT_STREQ("A,B,C", GetDisplayedCookies(cookies_model.get()).c_str());
    EXPECT_EQ("db1,db2", GetDisplayedDatabases(cookies_model.get()));
    EXPECT_EQ("http://host1:1/,http://host2:2/",
              GetDisplayedLocalStorages(cookies_model.get()));
    EXPECT_EQ("http://host1:1/,http://host2:2/",
              GetDisplayedSessionStorages(cookies_model.get()));
    EXPECT_EQ("http://fshost1:1/,http://fshost2:2/,http://fshost3:3/",
              GetDisplayedFileSystems(cookies_model.get()));
    EXPECT_EQ("http://idbhost1:1/,http://idbhost2:2/",
              GetDisplayedIndexedDBs(cookies_model.get()));
    EXPECT_EQ("quotahost1",
              GetDisplayedQuotas(cookies_model.get()));
    EXPECT_EQ(43, cookies_model->GetRoot()->GetTotalNodeCount());
  }
  DeleteStoredObjects(cookies_model->GetRoot()->GetChild(12));
  {
    SCOPED_TRACE("`quotahost1` removed.");
    EXPECT_STREQ("A,B,C", GetDisplayedCookies(cookies_model.get()).c_str());
    EXPECT_EQ("db1,db2", GetDisplayedDatabases(cookies_model.get()));
    EXPECT_EQ("http://host1:1/,http://host2:2/",
              GetDisplayedLocalStorages(cookies_model.get()));
    EXPECT_EQ("http://host1:1/,http://host2:2/",
              GetDisplayedSessionStorages(cookies_model.get()));
    EXPECT_EQ("http://fshost1:1/,http://fshost2:2/,http://fshost3:3/",
              GetDisplayedFileSystems(cookies_model.get()));
    EXPECT_EQ("http://idbhost1:1/,http://idbhost2:2/",
              GetDisplayedIndexedDBs(cookies_model.get()));
    EXPECT_EQ(41, cookies_model->GetRoot()->GetTotalNodeCount());
  }
  DeleteStoredObjects(cookies_model->GetRoot()->GetChild(11));
  {
    SCOPED_TRACE("`idbhost2` removed.");
    EXPECT_STREQ("A,B,C", GetDisplayedCookies(cookies_model.get()).c_str());
    EXPECT_EQ("db1,db2", GetDisplayedDatabases(cookies_model.get()));
    EXPECT_EQ("http://host1:1/,http://host2:2/",
              GetDisplayedLocalStorages(cookies_model.get()));
    EXPECT_EQ("http://host1:1/,http://host2:2/",
              GetDisplayedSessionStorages(cookies_model.get()));
    EXPECT_EQ("http://fshost1:1/,http://fshost2:2/,http://fshost3:3/",
              GetDisplayedFileSystems(cookies_model.get()));
    EXPECT_EQ("http://idbhost1:1/",
              GetDisplayedIndexedDBs(cookies_model.get()));
    EXPECT_EQ(38, cookies_model->GetRoot()->GetTotalNodeCount());
  }
  DeleteStoredObjects(cookies_model->GetRoot()->GetChild(10));
  {
    SCOPED_TRACE("`idbhost1` removed.");
    EXPECT_STREQ("A,B,C", GetDisplayedCookies(cookies_model.get()).c_str());
    EXPECT_EQ("db1,db2", GetDisplayedDatabases(cookies_model.get()));
    EXPECT_EQ("http://host1:1/,http://host2:2/",
              GetDisplayedLocalStorages(cookies_model.get()));
    EXPECT_EQ("http://host1:1/,http://host2:2/",
              GetDisplayedSessionStorages(cookies_model.get()));
    EXPECT_EQ("http://fshost1:1/,http://fshost2:2/,http://fshost3:3/",
              GetDisplayedFileSystems(cookies_model.get()));
    EXPECT_EQ("", GetDisplayedIndexedDBs(cookies_model.get()));
    EXPECT_EQ(35, cookies_model->GetRoot()->GetTotalNodeCount());
  }
  DeleteStoredObjects(cookies_model->GetRoot()->GetChild(9));
  {
    SCOPED_TRACE("`host2` removed.");
    EXPECT_STREQ("A,B,C", GetDisplayedCookies(cookies_model.get()).c_str());
    EXPECT_EQ("db1,db2", GetDisplayedDatabases(cookies_model.get()));
    EXPECT_EQ("http://host1:1/",
              GetDisplayedLocalStorages(cookies_model.get()));
    EXPECT_EQ("http://host1:1/",
              GetDisplayedSessionStorages(cookies_model.get()));
    EXPECT_EQ("http://fshost1:1/,http://fshost2:2/,http://fshost3:3/",
              GetDisplayedFileSystems(cookies_model.get()));
    EXPECT_EQ("", GetDisplayedIndexedDBs(cookies_model.get()));
    EXPECT_EQ(30, cookies_model->GetRoot()->GetTotalNodeCount());
  }
  DeleteStoredObjects(cookies_model->GetRoot()->GetChild(8));
  {
    SCOPED_TRACE("`host1` removed.");
    EXPECT_STREQ("A,B,C", GetDisplayedCookies(cookies_model.get()).c_str());
    EXPECT_EQ("db1,db2", GetDisplayedDatabases(cookies_model.get()));
    EXPECT_EQ("", GetDisplayedLocalStorages(cookies_model.get()));
    EXPECT_EQ("", GetDisplayedSessionStorages(cookies_model.get()));
    EXPECT_EQ("http://fshost1:1/,http://fshost2:2/,http://fshost3:3/",
              GetDisplayedFileSystems(cookies_model.get()));
    EXPECT_EQ("", GetDisplayedIndexedDBs(cookies_model.get()));
    EXPECT_EQ(25, cookies_model->GetRoot()->GetTotalNodeCount());
  }
  DeleteStoredObjects(cookies_model->GetRoot()->GetChild(7));
  {
    SCOPED_TRACE("`gdbhost2` removed.");
    EXPECT_STREQ("A,B,C", GetDisplayedCookies(cookies_model.get()).c_str());
    EXPECT_EQ("db1", GetDisplayedDatabases(cookies_model.get()));
    EXPECT_EQ("", GetDisplayedLocalStorages(cookies_model.get()));
    EXPECT_EQ("", GetDisplayedSessionStorages(cookies_model.get()));
    EXPECT_EQ("http://fshost1:1/,http://fshost2:2/,http://fshost3:3/",
              GetDisplayedFileSystems(cookies_model.get()));
    EXPECT_EQ("", GetDisplayedIndexedDBs(cookies_model.get()));
    EXPECT_EQ(22, cookies_model->GetRoot()->GetTotalNodeCount());
  }
  DeleteStoredObjects(cookies_model->GetRoot()->GetChild(6));
  {
    SCOPED_TRACE("`gdbhost1` removed.");
    EXPECT_STREQ("A,B,C", GetDisplayedCookies(cookies_model.get()).c_str());
    EXPECT_EQ("", GetDisplayedDatabases(cookies_model.get()));
    EXPECT_EQ("", GetDisplayedLocalStorages(cookies_model.get()));
    EXPECT_EQ("", GetDisplayedSessionStorages(cookies_model.get()));
    EXPECT_EQ("http://fshost1:1/,http://fshost2:2/,http://fshost3:3/",
              GetDisplayedFileSystems(cookies_model.get()));
    EXPECT_EQ("", GetDisplayedIndexedDBs(cookies_model.get()));
    EXPECT_EQ(19, cookies_model->GetRoot()->GetTotalNodeCount());
  }
  DeleteStoredObjects(cookies_model->GetRoot()->GetChild(5));
  {
    SCOPED_TRACE("`fshost3` removed.");
    EXPECT_STREQ("A,B,C", GetDisplayedCookies(cookies_model.get()).c_str());
    EXPECT_EQ("", GetDisplayedDatabases(cookies_model.get()));
    EXPECT_EQ("", GetDisplayedLocalStorages(cookies_model.get()));
    EXPECT_EQ("", GetDisplayedSessionStorages(cookies_model.get()));
    EXPECT_EQ("http://fshost1:1/,http://fshost2:2/",
              GetDisplayedFileSystems(cookies_model.get()));
    EXPECT_EQ("", GetDisplayedIndexedDBs(cookies_model.get()));
    EXPECT_EQ(16, cookies_model->GetRoot()->GetTotalNodeCount());
  }
  DeleteStoredObjects(cookies_model->GetRoot()->GetChild(4));
  {
    SCOPED_TRACE("`fshost2` removed.");
    EXPECT_STREQ("A,B,C", GetDisplayedCookies(cookies_model.get()).c_str());
    EXPECT_EQ("", GetDisplayedDatabases(cookies_model.get()));
    EXPECT_EQ("", GetDisplayedLocalStorages(cookies_model.get()));
    EXPECT_EQ("", GetDisplayedSessionStorages(cookies_model.get()));
    EXPECT_EQ("http://fshost1:1/",
              GetDisplayedFileSystems(cookies_model.get()));
    EXPECT_EQ("", GetDisplayedIndexedDBs(cookies_model.get()));
    EXPECT_EQ(13, cookies_model->GetRoot()->GetTotalNodeCount());
  }
  DeleteStoredObjects(cookies_model->GetRoot()->GetChild(3));
  {
    SCOPED_TRACE("`fshost1` removed.");
    EXPECT_STREQ("A,B,C", GetDisplayedCookies(cookies_model.get()).c_str());
    EXPECT_EQ("", GetDisplayedDatabases(cookies_model.get()));
    EXPECT_EQ("", GetDisplayedLocalStorages(cookies_model.get()));
    EXPECT_EQ("", GetDisplayedSessionStorages(cookies_model.get()));
    EXPECT_EQ("", GetDisplayedFileSystems(cookies_model.get()));
    EXPECT_EQ("", GetDisplayedIndexedDBs(cookies_model.get()));
    EXPECT_EQ(10, cookies_model->GetRoot()->GetTotalNodeCount());
  }
  DeleteStoredObjects(cookies_model->GetRoot()->GetChild(2));
  {
    SCOPED_TRACE("`foo3` removed.");
    EXPECT_STREQ("A,B", GetDisplayedCookies(cookies_model.get()).c_str());
    EXPECT_EQ("", GetDisplayedDatabases(cookies_model.get()));
    EXPECT_EQ("", GetDisplayedLocalStorages(cookies_model.get()));
    EXPECT_EQ("", GetDisplayedSessionStorages(cookies_model.get()));
    EXPECT_EQ("", GetDisplayedFileSystems(cookies_model.get()));
    EXPECT_EQ("", GetDisplayedIndexedDBs(cookies_model.get()));
    EXPECT_EQ(7, cookies_model->GetRoot()->GetTotalNodeCount());
  }
  DeleteStoredObjects(cookies_model->GetRoot()->GetChild(1));
  {
    SCOPED_TRACE("`foo2` removed.");
    EXPECT_STREQ("A", GetDisplayedCookies(cookies_model.get()).c_str());
    EXPECT_EQ("", GetDisplayedDatabases(cookies_model.get()));
    EXPECT_EQ("", GetDisplayedLocalStorages(cookies_model.get()));
    EXPECT_EQ("", GetDisplayedSessionStorages(cookies_model.get()));
    EXPECT_EQ("", GetDisplayedFileSystems(cookies_model.get()));
    EXPECT_EQ("", GetDisplayedIndexedDBs(cookies_model.get()));
    EXPECT_EQ(4, cookies_model->GetRoot()->GetTotalNodeCount());
  }
  DeleteStoredObjects(cookies_model->GetRoot()->GetChild(0));
  {
    SCOPED_TRACE("`foo1` removed.");
    EXPECT_STREQ("", GetDisplayedCookies(cookies_model.get()).c_str());
    EXPECT_EQ("", GetDisplayedDatabases(cookies_model.get()));
    EXPECT_EQ("", GetDisplayedLocalStorages(cookies_model.get()));
    EXPECT_EQ("", GetDisplayedSessionStorages(cookies_model.get()));
    EXPECT_EQ("", GetDisplayedFileSystems(cookies_model.get()));
    EXPECT_EQ("", GetDisplayedIndexedDBs(cookies_model.get()));
    EXPECT_EQ(1, cookies_model->GetRoot()->GetTotalNodeCount());
  }
}

TEST_F(CookiesTreeModelTest, RemoveCookiesNode) {
  scoped_ptr<CookiesTreeModel> cookies_model(
      CreateCookiesTreeModelWithInitialSample(false));

  DeleteStoredObjects(
      cookies_model->GetRoot()->GetChild(0)->GetChild(0));
  {
    SCOPED_TRACE("First origin removed");
    EXPECT_STREQ("B,C", GetDisplayedCookies(cookies_model.get()).c_str());
    // 51 because in this case, the origin remains, although the COOKIES
    // node beneath it has been deleted.
    EXPECT_EQ(51, cookies_model->GetRoot()->GetTotalNodeCount());
    EXPECT_EQ("db1,db2", GetDisplayedDatabases(cookies_model.get()));
    EXPECT_EQ("http://host1:1/,http://host2:2/",
              GetDisplayedLocalStorages(cookies_model.get()));
    EXPECT_EQ("http://host1:1/,http://host2:2/",
              GetDisplayedSessionStorages(cookies_model.get()));
    EXPECT_EQ("http://idbhost1:1/,http://idbhost2:2/",
              GetDisplayedIndexedDBs(cookies_model.get()));
    EXPECT_EQ("http://fshost1:1/,http://fshost2:2/,http://fshost3:3/",
              GetDisplayedFileSystems(cookies_model.get()));
    EXPECT_EQ("quotahost1,quotahost2", GetDisplayedQuotas(cookies_model.get()));
    EXPECT_EQ("sbc1,sbc2", GetDisplayedServerBoundCerts(cookies_model.get()));
  }

  DeleteStoredObjects(
      cookies_model->GetRoot()->GetChild(6)->GetChild(0));
  {
    SCOPED_TRACE("First database removed");
    EXPECT_STREQ("B,C", GetDisplayedCookies(cookies_model.get()).c_str());
    EXPECT_EQ("db2", GetDisplayedDatabases(cookies_model.get()));
    EXPECT_EQ("http://host1:1/,http://host2:2/",
              GetDisplayedLocalStorages(cookies_model.get()));
    EXPECT_EQ("http://host1:1/,http://host2:2/",
              GetDisplayedSessionStorages(cookies_model.get()));
    EXPECT_EQ("http://idbhost1:1/,http://idbhost2:2/",
              GetDisplayedIndexedDBs(cookies_model.get()));
    EXPECT_EQ("http://fshost1:1/,http://fshost2:2/,http://fshost3:3/",
              GetDisplayedFileSystems(cookies_model.get()));
    EXPECT_EQ("quotahost1,quotahost2", GetDisplayedQuotas(cookies_model.get()));
    EXPECT_EQ("sbc1,sbc2", GetDisplayedServerBoundCerts(cookies_model.get()));
    EXPECT_EQ(49, cookies_model->GetRoot()->GetTotalNodeCount());
  }

  DeleteStoredObjects(
      cookies_model->GetRoot()->GetChild(8)->GetChild(0));
  {
    SCOPED_TRACE("First origin removed");
    EXPECT_STREQ("B,C", GetDisplayedCookies(cookies_model.get()).c_str());
    EXPECT_EQ("db2", GetDisplayedDatabases(cookies_model.get()));
    EXPECT_EQ("http://host2:2/",
              GetDisplayedLocalStorages(cookies_model.get()));
    EXPECT_EQ("http://host1:1/,http://host2:2/",
              GetDisplayedSessionStorages(cookies_model.get()));
    EXPECT_EQ("http://idbhost1:1/,http://idbhost2:2/",
              GetDisplayedIndexedDBs(cookies_model.get()));
    EXPECT_EQ("http://fshost1:1/,http://fshost2:2/,http://fshost3:3/",
              GetDisplayedFileSystems(cookies_model.get()));
    EXPECT_EQ("quotahost1,quotahost2", GetDisplayedQuotas(cookies_model.get()));
    EXPECT_EQ("sbc1,sbc2", GetDisplayedServerBoundCerts(cookies_model.get()));
    EXPECT_EQ(47, cookies_model->GetRoot()->GetTotalNodeCount());
  }
}

TEST_F(CookiesTreeModelTest, RemoveCookieNode) {
  scoped_ptr<CookiesTreeModel> cookies_model(
      CreateCookiesTreeModelWithInitialSample(false));

  DeleteStoredObjects(
      cookies_model->GetRoot()->GetChild(1)->GetChild(0));
  {
    SCOPED_TRACE("Second origin COOKIES node removed");
    EXPECT_STREQ("A,C", GetDisplayedCookies(cookies_model.get()).c_str());
    EXPECT_EQ("db1,db2", GetDisplayedDatabases(cookies_model.get()));
    EXPECT_EQ("http://host1:1/,http://host2:2/",
              GetDisplayedLocalStorages(cookies_model.get()));
    EXPECT_EQ("http://host1:1/,http://host2:2/",
              GetDisplayedSessionStorages(cookies_model.get()));
    EXPECT_EQ("http://idbhost1:1/,http://idbhost2:2/",
              GetDisplayedIndexedDBs(cookies_model.get()));
    EXPECT_EQ("http://fshost1:1/,http://fshost2:2/,http://fshost3:3/",
              GetDisplayedFileSystems(cookies_model.get()));
    EXPECT_EQ("quotahost1,quotahost2", GetDisplayedQuotas(cookies_model.get()));
    EXPECT_EQ("sbc1,sbc2", GetDisplayedServerBoundCerts(cookies_model.get()));
    // 51 because in this case, the origin remains, although the COOKIES
    // node beneath it has been deleted.
    EXPECT_EQ(51, cookies_model->GetRoot()->GetTotalNodeCount());
  }

  DeleteStoredObjects(
      cookies_model->GetRoot()->GetChild(6)->GetChild(0));
  {
    SCOPED_TRACE("First database removed");
    EXPECT_STREQ("A,C", GetDisplayedCookies(cookies_model.get()).c_str());
    EXPECT_EQ("db2", GetDisplayedDatabases(cookies_model.get()));
    EXPECT_EQ("http://host1:1/,http://host2:2/",
              GetDisplayedLocalStorages(cookies_model.get()));
    EXPECT_EQ("http://host1:1/,http://host2:2/",
              GetDisplayedSessionStorages(cookies_model.get()));
    EXPECT_EQ("http://idbhost1:1/,http://idbhost2:2/",
              GetDisplayedIndexedDBs(cookies_model.get()));
    EXPECT_EQ("http://fshost1:1/,http://fshost2:2/,http://fshost3:3/",
              GetDisplayedFileSystems(cookies_model.get()));
    EXPECT_EQ("quotahost1,quotahost2", GetDisplayedQuotas(cookies_model.get()));
    EXPECT_EQ("sbc1,sbc2", GetDisplayedServerBoundCerts(cookies_model.get()));
    EXPECT_EQ(49, cookies_model->GetRoot()->GetTotalNodeCount());
  }

  DeleteStoredObjects(
      cookies_model->GetRoot()->GetChild(8)->GetChild(0));
  {
    SCOPED_TRACE("First origin removed");
    EXPECT_STREQ("A,C", GetDisplayedCookies(cookies_model.get()).c_str());
    EXPECT_EQ("db2", GetDisplayedDatabases(cookies_model.get()));
    EXPECT_EQ("http://host2:2/",
              GetDisplayedLocalStorages(cookies_model.get()));
    EXPECT_EQ("http://host1:1/,http://host2:2/",
              GetDisplayedSessionStorages(cookies_model.get()));
    EXPECT_EQ("http://idbhost1:1/,http://idbhost2:2/",
              GetDisplayedIndexedDBs(cookies_model.get()));
    EXPECT_EQ("http://fshost1:1/,http://fshost2:2/,http://fshost3:3/",
              GetDisplayedFileSystems(cookies_model.get()));
    EXPECT_EQ("quotahost1,quotahost2", GetDisplayedQuotas(cookies_model.get()));
    EXPECT_EQ("sbc1,sbc2", GetDisplayedServerBoundCerts(cookies_model.get()));
    EXPECT_EQ(47, cookies_model->GetRoot()->GetTotalNodeCount());
  }
}

TEST_F(CookiesTreeModelTest, RemoveSingleCookieNode) {
  ContainerMap container_map;

  container_map[std::string()] = new LocalDataContainer(
      "Drive-By-Web", std::string(),
      mock_browsing_data_cookie_helper_,
      mock_browsing_data_database_helper_,
      mock_browsing_data_local_storage_helper_,
      mock_browsing_data_session_storage_helper_,
      mock_browsing_data_appcache_helper_,
      mock_browsing_data_indexed_db_helper_,
      mock_browsing_data_file_system_helper_,
      mock_browsing_data_quota_helper_,
      mock_browsing_data_server_bound_cert_helper_,
      mock_browsing_data_flash_lso_helper_);
  CookiesTreeModel cookies_model(container_map, special_storage_policy_, false);

  mock_browsing_data_cookie_helper_->
      AddCookieSamples(GURL("http://foo1"), "A=1");
  mock_browsing_data_cookie_helper_->
      AddCookieSamples(GURL("http://foo2"), "B=1");
  mock_browsing_data_cookie_helper_->
      AddCookieSamples(GURL("http://foo3"), "C=1");
  mock_browsing_data_cookie_helper_->
      AddCookieSamples(GURL("http://foo3"), "D=1");
  mock_browsing_data_cookie_helper_->Notify();
  mock_browsing_data_database_helper_->AddDatabaseSamples();
  mock_browsing_data_database_helper_->Notify();
  mock_browsing_data_local_storage_helper_->AddLocalStorageSamples();
  mock_browsing_data_local_storage_helper_->Notify();
  mock_browsing_data_session_storage_helper_->AddLocalStorageSamples();
  mock_browsing_data_session_storage_helper_->Notify();
  mock_browsing_data_indexed_db_helper_->AddIndexedDBSamples();
  mock_browsing_data_indexed_db_helper_->Notify();
  mock_browsing_data_file_system_helper_->AddFileSystemSamples();
  mock_browsing_data_file_system_helper_->Notify();
  mock_browsing_data_quota_helper_->AddQuotaSamples();
  mock_browsing_data_quota_helper_->Notify();

  {
    SCOPED_TRACE("Initial State 4 cookies, 2 databases, 2 local storages, "
                 "2 session storages, 2 indexed DBs, 3 file systems, "
                 "2 quotas.");
    // 46 because there's the root, then
    // foo1 -> cookies -> a,
    // foo2 -> cookies -> b,
    // foo3 -> cookies -> c,d
    // dbhost1 -> database -> db1,
    // dbhost2 -> database -> db2,
    // host1 -> localstorage -> http://host1:1/,
    //       -> sessionstorage -> http://host1:1/,
    // host2 -> localstorage -> http://host2:2/,
    //       -> sessionstorage -> http://host2:2/,
    // idbhost1 -> sessionstorage -> http://idbhost1:1/,
    // idbhost2 -> sessionstorage -> http://idbhost2:2/,
    // fshost1 -> filesystem -> http://fshost1:1/,
    // fshost2 -> filesystem -> http://fshost2:1/,
    // fshost3 -> filesystem -> http://fshost3:1/,
    // quotahost1 -> quotahost1,
    // quotahost2 -> quotahost2.
    EXPECT_EQ(46, cookies_model.GetRoot()->GetTotalNodeCount());
    EXPECT_STREQ("A,B,C,D", GetDisplayedCookies(&cookies_model).c_str());
    EXPECT_EQ("db1,db2", GetDisplayedDatabases(&cookies_model));
    EXPECT_EQ("http://host1:1/,http://host2:2/",
              GetDisplayedLocalStorages(&cookies_model));
    EXPECT_EQ("http://host1:1/,http://host2:2/",
              GetDisplayedSessionStorages(&cookies_model));
    EXPECT_EQ("http://idbhost1:1/,http://idbhost2:2/",
              GetDisplayedIndexedDBs(&cookies_model));
    EXPECT_EQ("http://fshost1:1/,http://fshost2:2/,http://fshost3:3/",
              GetDisplayedFileSystems(&cookies_model));
    EXPECT_EQ("quotahost1,quotahost2", GetDisplayedQuotas(&cookies_model));
  }
  DeleteStoredObjects(cookies_model.GetRoot()->GetChild(2));
  {
    SCOPED_TRACE("Third origin removed");
    EXPECT_STREQ("A,B", GetDisplayedCookies(&cookies_model).c_str());
    EXPECT_EQ("db1,db2", GetDisplayedDatabases(&cookies_model));
    EXPECT_EQ("http://host1:1/,http://host2:2/",
              GetDisplayedLocalStorages(&cookies_model));
    EXPECT_EQ("http://host1:1/,http://host2:2/",
              GetDisplayedSessionStorages(&cookies_model));
    EXPECT_EQ("http://idbhost1:1/,http://idbhost2:2/",
              GetDisplayedIndexedDBs(&cookies_model));
    EXPECT_EQ("http://fshost1:1/,http://fshost2:2/,http://fshost3:3/",
              GetDisplayedFileSystems(&cookies_model));
    EXPECT_EQ("quotahost1,quotahost2", GetDisplayedQuotas(&cookies_model));
    EXPECT_EQ(42, cookies_model.GetRoot()->GetTotalNodeCount());
  }
}

TEST_F(CookiesTreeModelTest, RemoveSingleCookieNodeOf3) {
  ContainerMap container_map;

  container_map[std::string()] = new LocalDataContainer(
      "Drive-By-Web", std::string(),
      mock_browsing_data_cookie_helper_,
      mock_browsing_data_database_helper_,
      mock_browsing_data_local_storage_helper_,
      mock_browsing_data_session_storage_helper_,
      mock_browsing_data_appcache_helper_,
      mock_browsing_data_indexed_db_helper_,
      mock_browsing_data_file_system_helper_,
      mock_browsing_data_quota_helper_,
      mock_browsing_data_server_bound_cert_helper_,
      mock_browsing_data_flash_lso_helper_);
  CookiesTreeModel cookies_model(container_map, special_storage_policy_,false);

  mock_browsing_data_cookie_helper_->
      AddCookieSamples(GURL("http://foo1"), "A=1");
  mock_browsing_data_cookie_helper_->
      AddCookieSamples(GURL("http://foo2"), "B=1");
  mock_browsing_data_cookie_helper_->
      AddCookieSamples(GURL("http://foo3"), "C=1");
  mock_browsing_data_cookie_helper_->
      AddCookieSamples(GURL("http://foo3"), "D=1");
  mock_browsing_data_cookie_helper_->
      AddCookieSamples(GURL("http://foo3"), "E=1");
  mock_browsing_data_cookie_helper_->Notify();
  mock_browsing_data_database_helper_->AddDatabaseSamples();
  mock_browsing_data_database_helper_->Notify();
  mock_browsing_data_local_storage_helper_->AddLocalStorageSamples();
  mock_browsing_data_local_storage_helper_->Notify();
  mock_browsing_data_session_storage_helper_->AddLocalStorageSamples();
  mock_browsing_data_session_storage_helper_->Notify();
  mock_browsing_data_indexed_db_helper_->AddIndexedDBSamples();
  mock_browsing_data_indexed_db_helper_->Notify();
  mock_browsing_data_file_system_helper_->AddFileSystemSamples();
  mock_browsing_data_file_system_helper_->Notify();
  mock_browsing_data_quota_helper_->AddQuotaSamples();
  mock_browsing_data_quota_helper_->Notify();

  {
    SCOPED_TRACE("Initial State 5 cookies, 2 databases, 2 local storages, "
                 "2 session storages, 2 indexed DBs, 3 filesystems, "
                 "2 quotas.");
    // 47 because there's the root, then
    // foo1 -> cookies -> a,
    // foo2 -> cookies -> b,
    // foo3 -> cookies -> c,d,e
    // dbhost1 -> database -> db1,
    // dbhost2 -> database -> db2,
    // host1 -> localstorage -> http://host1:1/,
    //       -> sessionstorage -> http://host1:1/,
    // host2 -> localstorage -> http://host2:2/,
    //       -> sessionstorage -> http://host2:2/,
    // idbhost1 -> sessionstorage -> http://idbhost1:1/,
    // idbhost2 -> sessionstorage -> http://idbhost2:2/,
    // fshost1 -> filesystem -> http://fshost1:1/,
    // fshost2 -> filesystem -> http://fshost2:1/,
    // fshost3 -> filesystem -> http://fshost3:1/,
    // quotahost1 -> quotahost1,
    // quotahost2 -> quotahost2.
    EXPECT_EQ(47, cookies_model.GetRoot()->GetTotalNodeCount());
    EXPECT_STREQ("A,B,C,D,E", GetDisplayedCookies(&cookies_model).c_str());
    EXPECT_EQ("db1,db2", GetDisplayedDatabases(&cookies_model));
    EXPECT_EQ("http://host1:1/,http://host2:2/",
              GetDisplayedLocalStorages(&cookies_model));
    EXPECT_EQ("http://host1:1/,http://host2:2/",
              GetDisplayedSessionStorages(&cookies_model));
    EXPECT_EQ("http://idbhost1:1/,http://idbhost2:2/",
              GetDisplayedIndexedDBs(&cookies_model));
    EXPECT_EQ("http://fshost1:1/,http://fshost2:2/,http://fshost3:3/",
              GetDisplayedFileSystems(&cookies_model));
    EXPECT_EQ("quotahost1,quotahost2", GetDisplayedQuotas(&cookies_model));
  }
  DeleteStoredObjects(cookies_model.GetRoot()->GetChild(2)->GetChild(0)->
      GetChild(1));
  {
    SCOPED_TRACE("Middle cookie in third origin removed");
    EXPECT_STREQ("A,B,C,E", GetDisplayedCookies(&cookies_model).c_str());
    EXPECT_EQ(46, cookies_model.GetRoot()->GetTotalNodeCount());
    EXPECT_EQ("db1,db2", GetDisplayedDatabases(&cookies_model));
    EXPECT_EQ("http://host1:1/,http://host2:2/",
              GetDisplayedLocalStorages(&cookies_model));
    EXPECT_EQ("http://host1:1/,http://host2:2/",
              GetDisplayedSessionStorages(&cookies_model));
    EXPECT_EQ("http://idbhost1:1/,http://idbhost2:2/",
              GetDisplayedIndexedDBs(&cookies_model));
    EXPECT_EQ("http://fshost1:1/,http://fshost2:2/,http://fshost3:3/",
              GetDisplayedFileSystems(&cookies_model));
    EXPECT_EQ("quotahost1,quotahost2", GetDisplayedQuotas(&cookies_model));
  }
}

TEST_F(CookiesTreeModelTest, RemoveSecondOrigin) {
  ContainerMap container_map;

  container_map[std::string()] = new LocalDataContainer(
      "Drive-By-Web", std::string(),
      mock_browsing_data_cookie_helper_,
      mock_browsing_data_database_helper_,
      mock_browsing_data_local_storage_helper_,
      mock_browsing_data_session_storage_helper_,
      mock_browsing_data_appcache_helper_,
      mock_browsing_data_indexed_db_helper_,
      mock_browsing_data_file_system_helper_,
      mock_browsing_data_quota_helper_,
      mock_browsing_data_server_bound_cert_helper_,
      mock_browsing_data_flash_lso_helper_);
  CookiesTreeModel cookies_model(container_map, special_storage_policy_,false);

  mock_browsing_data_cookie_helper_->
      AddCookieSamples(GURL("http://foo1"), "A=1");
  mock_browsing_data_cookie_helper_->
      AddCookieSamples(GURL("http://foo2"), "B=1");
  mock_browsing_data_cookie_helper_->
      AddCookieSamples(GURL("http://foo3"), "C=1");
  mock_browsing_data_cookie_helper_->
      AddCookieSamples(GURL("http://foo3"), "D=1");
  mock_browsing_data_cookie_helper_->
      AddCookieSamples(GURL("http://foo3"), "E=1");
  mock_browsing_data_cookie_helper_->Notify();

  {
    SCOPED_TRACE("Initial State 5 cookies");
    // 12 because there's the root, then foo1 -> cookies -> a,
    // foo2 -> cookies -> b, foo3 -> cookies -> c,d,e
    EXPECT_EQ(12, cookies_model.GetRoot()->GetTotalNodeCount());
    EXPECT_STREQ("A,B,C,D,E", GetDisplayedCookies(&cookies_model).c_str());
  }
  DeleteStoredObjects(cookies_model.GetRoot()->GetChild(1));
  {
    SCOPED_TRACE("Second origin removed");
    EXPECT_STREQ("A,C,D,E", GetDisplayedCookies(&cookies_model).c_str());
    // Left with root -> foo1 -> cookies -> a, foo3 -> cookies -> c,d,e
    EXPECT_EQ(9, cookies_model.GetRoot()->GetTotalNodeCount());
  }
}

TEST_F(CookiesTreeModelTest, OriginOrdering) {
  ContainerMap container_map;

  container_map[std::string()] = new LocalDataContainer(
      "Drive-By-Web", std::string(),
      mock_browsing_data_cookie_helper_,
      mock_browsing_data_database_helper_,
      mock_browsing_data_local_storage_helper_,
      mock_browsing_data_session_storage_helper_,
      mock_browsing_data_appcache_helper_,
      mock_browsing_data_indexed_db_helper_,
      mock_browsing_data_file_system_helper_,
      mock_browsing_data_quota_helper_,
      mock_browsing_data_server_bound_cert_helper_,
      mock_browsing_data_flash_lso_helper_);
  CookiesTreeModel cookies_model(container_map, special_storage_policy_, false);

  mock_browsing_data_cookie_helper_->
      AddCookieSamples(GURL("http://a.foo2.com"), "A=1");
  mock_browsing_data_cookie_helper_->
      AddCookieSamples(GURL("http://foo2.com"), "B=1");
  mock_browsing_data_cookie_helper_->
      AddCookieSamples(GURL("http://b.foo1.com"), "C=1");
  // Leading dot on the foo4
  mock_browsing_data_cookie_helper_->AddCookieSamples(
      GURL("http://foo4.com"), "D=1; domain=.foo4.com; path=/;");
  mock_browsing_data_cookie_helper_->
      AddCookieSamples(GURL("http://a.foo1.com"), "E=1");
  mock_browsing_data_cookie_helper_->
      AddCookieSamples(GURL("http://foo1.com"), "F=1");
  mock_browsing_data_cookie_helper_->
      AddCookieSamples(GURL("http://foo3.com"), "G=1");
  mock_browsing_data_cookie_helper_->
      AddCookieSamples(GURL("http://foo4.com"), "H=1");
  mock_browsing_data_cookie_helper_->Notify();

  {
    SCOPED_TRACE("Initial State 8 cookies");
    EXPECT_EQ(23, cookies_model.GetRoot()->GetTotalNodeCount());
    EXPECT_STREQ("F,E,C,B,A,G,D,H",
        GetDisplayedCookies(&cookies_model).c_str());
  }
  // Delete "E"
  DeleteStoredObjects(cookies_model.GetRoot()->GetChild(1));
  {
    EXPECT_STREQ("F,C,B,A,G,D,H", GetDisplayedCookies(&cookies_model).c_str());
  }
}

TEST_F(CookiesTreeModelTest, ContentSettings) {
  GURL host("http://xyz.com/");
  std::string name = "Drive-By-Web";
  std::string browser_id;
  ContainerMap container_map;

  container_map[browser_id] = new LocalDataContainer(
      name, browser_id,
      mock_browsing_data_cookie_helper_,
      mock_browsing_data_database_helper_,
      mock_browsing_data_local_storage_helper_,
      mock_browsing_data_session_storage_helper_,
      mock_browsing_data_appcache_helper_,
      mock_browsing_data_indexed_db_helper_,
      mock_browsing_data_file_system_helper_,
      mock_browsing_data_quota_helper_,
      mock_browsing_data_server_bound_cert_helper_,
      mock_browsing_data_flash_lso_helper_);
  CookiesTreeModel cookies_model(container_map, special_storage_policy_, false);

  mock_browsing_data_cookie_helper_->AddCookieSamples(host, "A=1");
  mock_browsing_data_cookie_helper_->Notify();

  TestingProfile profile;
  HostContentSettingsMap* content_settings =
      profile.GetHostContentSettingsMap();
  CookieSettings* cookie_settings =
      CookieSettings::Factory::GetForProfile(&profile);
  MockSettingsObserver observer;

  CookieTreeRootNode* root =
      static_cast<CookieTreeRootNode*>(cookies_model.GetRoot());
  CookieTreeHostNode* origin =
      root->GetOrCreateHostNode(host, browser_id, name);

  EXPECT_EQ(1, origin->child_count());
  EXPECT_TRUE(origin->CanCreateContentException());
  EXPECT_CALL(observer,
              OnContentSettingsChanged(
                  content_settings,
                  CONTENT_SETTINGS_TYPE_COOKIES,
                  false,
                  ContentSettingsPattern::FromURLNoWildcard(host),
                  ContentSettingsPattern::Wildcard(),
                  false));
  EXPECT_CALL(observer,
              OnContentSettingsChanged(content_settings,
                  CONTENT_SETTINGS_TYPE_COOKIES,
                  false,
                  ContentSettingsPattern::FromURL(host),
                  ContentSettingsPattern::Wildcard(),
                  false));
  origin->CreateContentException(
      cookie_settings, CONTENT_SETTING_SESSION_ONLY);
  EXPECT_TRUE(cookie_settings->IsReadingCookieAllowed(host, host));
  EXPECT_TRUE(cookie_settings->IsCookieSessionOnly(host));
}

TEST_F(CookiesTreeModelTest, AppOriginTitle) {
  scoped_ptr<CookiesTreeModel> cookies_model(
      CreateCookiesTreeModelWithInitialSample(true));

  EXPECT_EQ(ASCIIToUTF16("Isolated App, app-origin1"),
      cookies_model->GetRoot()->GetChild(17)->GetTitle());
  EXPECT_EQ(ASCIIToUTF16("Isolated App, app-origin2"),
      cookies_model->GetRoot()->GetChild(18)->GetTitle());
}

TEST_F(CookiesTreeModelTest, FileSystemFilter) {
  scoped_ptr<CookiesTreeModel> cookies_model(
      CreateCookiesTreeModelWithInitialSample(false));

  cookies_model->UpdateSearchResults(ASCIIToUTF16("fshost1"));
  EXPECT_EQ("http://fshost1:1/",
            GetDisplayedFileSystems(cookies_model.get()));

  cookies_model->UpdateSearchResults(ASCIIToUTF16("fshost2"));
  EXPECT_EQ("http://fshost2:2/",
            GetDisplayedFileSystems(cookies_model.get()));

  cookies_model->UpdateSearchResults(ASCIIToUTF16("fshost3"));
  EXPECT_EQ("http://fshost3:3/",
            GetDisplayedFileSystems(cookies_model.get()));

  cookies_model->UpdateSearchResults(string16());
  EXPECT_EQ("http://fshost1:1/,http://fshost2:2/,http://fshost3:3/",
            GetDisplayedFileSystems(cookies_model.get()));
}

TEST_F(CookiesTreeModelTest, CookiesFilter) {
  ContainerMap container_map;

  container_map[std::string()] = new LocalDataContainer(
      "Drive-By-Web", std::string(),
      mock_browsing_data_cookie_helper_,
      mock_browsing_data_database_helper_,
      mock_browsing_data_local_storage_helper_,
      mock_browsing_data_session_storage_helper_,
      mock_browsing_data_appcache_helper_,
      mock_browsing_data_indexed_db_helper_,
      mock_browsing_data_file_system_helper_,
      mock_browsing_data_quota_helper_,
      mock_browsing_data_server_bound_cert_helper_,
      mock_browsing_data_flash_lso_helper_);
  CookiesTreeModel cookies_model(container_map, special_storage_policy_, false);

  mock_browsing_data_cookie_helper_->
      AddCookieSamples(GURL("http://123.com"), "A=1");
  mock_browsing_data_cookie_helper_->
      AddCookieSamples(GURL("http://foo1.com"), "B=1");
  mock_browsing_data_cookie_helper_->
      AddCookieSamples(GURL("http://foo2.com"), "C=1");
  mock_browsing_data_cookie_helper_->
      AddCookieSamples(GURL("http://foo3.com"), "D=1");
  mock_browsing_data_cookie_helper_->Notify();
  EXPECT_EQ("A,B,C,D", GetDisplayedCookies(&cookies_model));

  cookies_model.UpdateSearchResults(string16(ASCIIToUTF16("foo")));
  EXPECT_EQ("B,C,D", GetDisplayedCookies(&cookies_model));

  cookies_model.UpdateSearchResults(string16(ASCIIToUTF16("2")));
  EXPECT_EQ("A,C", GetDisplayedCookies(&cookies_model));

  cookies_model.UpdateSearchResults(string16(ASCIIToUTF16("foo3")));
  EXPECT_EQ("D", GetDisplayedCookies(&cookies_model));

  cookies_model.UpdateSearchResults(string16());
  EXPECT_EQ("A,B,C,D", GetDisplayedCookies(&cookies_model));
}

}  // namespace
