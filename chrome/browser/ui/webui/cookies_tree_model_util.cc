// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/cookies_tree_model_util.h"

#include <vector>

#include "base/i18n/time_formatting.h"
#include "base/memory/scoped_ptr.h"
#include "base/string_number_conversions.h"
#include "base/string_split.h"
#include "base/string_util.h"
#include "base/values.h"
#include "chrome/browser/browsing_data/cookies_tree_model.h"
#include "grit/generated_resources.h"
#include "net/cookies/canonical_cookie.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/text/bytes_formatting.h"

namespace {

const char kKeyId[] = "id";
const char kKeyTitle[] = "title";
const char kKeyIcon[] = "icon";
const char kKeyType[] = "type";
const char kKeyHasChildren[] = "hasChildren";

const char kKeyAppId[] = "appId";

const char kKeyAppsProtectingThis[] = "appsProtectingThis";
const char kKeyName[] = "name";
const char kKeyContent[] = "content";
const char kKeyDomain[] = "domain";
const char kKeyPath[] = "path";
const char kKeySendFor[] = "sendfor";
const char kKeyAccessibleToScript[] = "accessibleToScript";
const char kKeyDesc[] = "desc";
const char kKeySize[] = "size";
const char kKeyOrigin[] = "origin";
const char kKeyManifest[] = "manifest";
const char kKeyServerId[] = "serverId";

const char kKeyAccessed[] = "accessed";
const char kKeyCreated[] = "created";
const char kKeyExpires[] = "expires";
const char kKeyModified[] = "modified";

const char kKeyPersistent[] = "persistent";
const char kKeyTemporary[] = "temporary";

const char kKeyTotalUsage[] = "totalUsage";
const char kKeyTemporaryUsage[] = "temporaryUsage";
const char kKeyPersistentUsage[] = "persistentUsage";
const char kKeyPersistentQuota[] = "persistentQuota";

const char kKeyCertType[] = "certType";

const int64 kNegligibleUsage = 1024;  // 1KiB

std::string ClientCertTypeToString(net::SSLClientCertType type) {
  switch (type) {
    case net::CLIENT_CERT_RSA_SIGN:
      return l10n_util::GetStringUTF8(IDS_CLIENT_CERT_RSA_SIGN);
    case net::CLIENT_CERT_ECDSA_SIGN:
      return l10n_util::GetStringUTF8(IDS_CLIENT_CERT_ECDSA_SIGN);
    default:
      return base::IntToString(type);
  }
}

}  // namespace

CookiesTreeModelUtil::CookiesTreeModelUtil() {
}

CookiesTreeModelUtil::~CookiesTreeModelUtil() {
}

std::string CookiesTreeModelUtil::GetTreeNodeId(const CookieTreeNode* node) {
  CookieTreeNodeMap::const_iterator iter = node_map_.find(node);
  if (iter != node_map_.end())
    return base::IntToString(iter->second);

  int32 new_id = id_map_.Add(node);
  node_map_[node] = new_id;
  return base::IntToString(new_id);
}

bool CookiesTreeModelUtil::GetCookieTreeNodeDictionary(
    const CookieTreeNode& node,
    base::DictionaryValue* dict) {
  // Use node's address as an id for WebUI to look it up.
  dict->SetString(kKeyId, GetTreeNodeId(&node));
  dict->SetString(kKeyTitle, node.GetTitle());
  dict->SetBoolean(kKeyHasChildren, !node.empty());

  switch (node.GetDetailedInfo().node_type) {
    case CookieTreeNode::DetailedInfo::TYPE_HOST: {
      dict->SetString(kKeyType, "origin");
      dict->SetString(kKeyAppId, node.GetDetailedInfo().app_id);
#if defined(OS_MACOSX)
      dict->SetString(kKeyIcon, "chrome://theme/IDR_BOOKMARK_BAR_FOLDER");
#endif
      break;
    }
    case CookieTreeNode::DetailedInfo::TYPE_COOKIE: {
      dict->SetString(kKeyType, "cookie");
      dict->SetString(kKeyIcon, "chrome://theme/IDR_COOKIE_ICON");

      const net::CanonicalCookie& cookie = *node.GetDetailedInfo().cookie;

      dict->SetString(kKeyName, cookie.Name());
      dict->SetString(kKeyContent, cookie.Value());
      dict->SetString(kKeyDomain, cookie.Domain());
      dict->SetString(kKeyPath, cookie.Path());
      dict->SetString(kKeySendFor, cookie.IsSecure() ?
          l10n_util::GetStringUTF8(IDS_COOKIES_COOKIE_SENDFOR_SECURE) :
          l10n_util::GetStringUTF8(IDS_COOKIES_COOKIE_SENDFOR_ANY));
      std::string accessible = cookie.IsHttpOnly() ?
          l10n_util::GetStringUTF8(IDS_COOKIES_COOKIE_ACCESSIBLE_TO_SCRIPT_NO) :
          l10n_util::GetStringUTF8(IDS_COOKIES_COOKIE_ACCESSIBLE_TO_SCRIPT_YES);
      dict->SetString(kKeyAccessibleToScript, accessible);
      dict->SetString(kKeyCreated, UTF16ToUTF8(
          base::TimeFormatFriendlyDateAndTime(cookie.CreationDate())));
      dict->SetString(kKeyExpires, cookie.IsPersistent() ? UTF16ToUTF8(
          base::TimeFormatFriendlyDateAndTime(cookie.ExpiryDate())) :
          l10n_util::GetStringUTF8(IDS_COOKIES_COOKIE_EXPIRES_SESSION));

      break;
    }
    case CookieTreeNode::DetailedInfo::TYPE_DATABASE: {
      dict->SetString(kKeyType, "database");
      dict->SetString(kKeyIcon, "chrome://theme/IDR_COOKIE_STORAGE_ICON");

      const BrowsingDataDatabaseHelper::DatabaseInfo& database_info =
          *node.GetDetailedInfo().database_info;

      dict->SetString(kKeyName, database_info.database_name.empty() ?
          l10n_util::GetStringUTF8(IDS_COOKIES_WEB_DATABASE_UNNAMED_NAME) :
          database_info.database_name);
      dict->SetString(kKeyDesc, database_info.description);
      dict->SetString(kKeySize, ui::FormatBytes(database_info.size));
      dict->SetString(kKeyModified, UTF16ToUTF8(
          base::TimeFormatFriendlyDateAndTime(database_info.last_modified)));

      break;
    }
    case CookieTreeNode::DetailedInfo::TYPE_LOCAL_STORAGE: {
      dict->SetString(kKeyType, "local_storage");
      dict->SetString(kKeyIcon, "chrome://theme/IDR_COOKIE_STORAGE_ICON");

      const BrowsingDataLocalStorageHelper::LocalStorageInfo&
         local_storage_info = *node.GetDetailedInfo().local_storage_info;

      dict->SetString(kKeyOrigin, local_storage_info.origin_url.spec());
      dict->SetString(kKeySize, ui::FormatBytes(local_storage_info.size));
      dict->SetString(kKeyModified, UTF16ToUTF8(
          base::TimeFormatFriendlyDateAndTime(
              local_storage_info.last_modified)));

      break;
    }
    case CookieTreeNode::DetailedInfo::TYPE_APPCACHE: {
      dict->SetString(kKeyType, "app_cache");
      dict->SetString(kKeyIcon, "chrome://theme/IDR_COOKIE_STORAGE_ICON");

      const appcache::AppCacheInfo& appcache_info =
          *node.GetDetailedInfo().appcache_info;

      dict->SetString(kKeyManifest, appcache_info.manifest_url.spec());
      dict->SetString(kKeySize, ui::FormatBytes(appcache_info.size));
      dict->SetString(kKeyCreated, UTF16ToUTF8(
          base::TimeFormatFriendlyDateAndTime(appcache_info.creation_time)));
      dict->SetString(kKeyAccessed, UTF16ToUTF8(
          base::TimeFormatFriendlyDateAndTime(appcache_info.last_access_time)));

      break;
    }
    case CookieTreeNode::DetailedInfo::TYPE_INDEXED_DB: {
      dict->SetString(kKeyType, "indexed_db");
      dict->SetString(kKeyIcon, "chrome://theme/IDR_COOKIE_STORAGE_ICON");

      const BrowsingDataIndexedDBHelper::IndexedDBInfo& indexed_db_info =
          *node.GetDetailedInfo().indexed_db_info;

      dict->SetString(kKeyOrigin, indexed_db_info.origin.spec());
      dict->SetString(kKeySize, ui::FormatBytes(indexed_db_info.size));
      dict->SetString(kKeyModified, UTF16ToUTF8(
          base::TimeFormatFriendlyDateAndTime(indexed_db_info.last_modified)));

      break;
    }
    case CookieTreeNode::DetailedInfo::TYPE_FILE_SYSTEM: {
      dict->SetString(kKeyType, "file_system");
      dict->SetString(kKeyIcon, "chrome://theme/IDR_COOKIE_STORAGE_ICON");

      const BrowsingDataFileSystemHelper::FileSystemInfo& file_system_info =
          *node.GetDetailedInfo().file_system_info;

      dict->SetString(kKeyOrigin, file_system_info.origin.spec());
      dict->SetString(kKeyPersistent,
                      file_system_info.has_persistent ?
                          UTF16ToUTF8(ui::FormatBytes(
                              file_system_info.usage_persistent)) :
                          l10n_util::GetStringUTF8(
                              IDS_COOKIES_FILE_SYSTEM_USAGE_NONE));
      dict->SetString(kKeyTemporary,
                      file_system_info.has_temporary ?
                          UTF16ToUTF8(ui::FormatBytes(
                              file_system_info.usage_temporary)) :
                          l10n_util::GetStringUTF8(
                              IDS_COOKIES_FILE_SYSTEM_USAGE_NONE));
      break;
    }
    case CookieTreeNode::DetailedInfo::TYPE_QUOTA: {
      dict->SetString(kKeyType, "quota");
      dict->SetString(kKeyIcon, "chrome://theme/IDR_COOKIE_STORAGE_ICON");

      const BrowsingDataQuotaHelper::QuotaInfo& quota_info =
          *node.GetDetailedInfo().quota_info;
      if (quota_info.temporary_usage + quota_info.persistent_usage <=
          kNegligibleUsage)
        return false;

      dict->SetString(kKeyOrigin, quota_info.host);
      dict->SetString(kKeyTotalUsage,
                      UTF16ToUTF8(ui::FormatBytes(
                          quota_info.temporary_usage +
                          quota_info.persistent_usage)));
      dict->SetString(kKeyTemporaryUsage,
                      UTF16ToUTF8(ui::FormatBytes(
                          quota_info.temporary_usage)));
      dict->SetString(kKeyPersistentUsage,
                      UTF16ToUTF8(ui::FormatBytes(
                          quota_info.persistent_usage)));
      break;
    }
    case CookieTreeNode::DetailedInfo::TYPE_SERVER_BOUND_CERT: {
      dict->SetString(kKeyType, "server_bound_cert");
      dict->SetString(kKeyIcon, "chrome://theme/IDR_COOKIE_ICON");

      const net::ServerBoundCertStore::ServerBoundCert& server_bound_cert =
          *node.GetDetailedInfo().server_bound_cert;

      dict->SetString(kKeyServerId, server_bound_cert.server_identifier());
      dict->SetString(kKeyCertType,
                      ClientCertTypeToString(server_bound_cert.type()));
      dict->SetString(kKeyCreated, UTF16ToUTF8(
          base::TimeFormatFriendlyDateAndTime(
              server_bound_cert.creation_time())));
      dict->SetString(kKeyExpires, UTF16ToUTF8(
          base::TimeFormatFriendlyDateAndTime(
              server_bound_cert.expiration_time())));
      break;
    }
    case CookieTreeNode::DetailedInfo::TYPE_FLASH_LSO: {
      dict->SetString(kKeyType, "flash_lso");
      dict->SetString(kKeyIcon, "chrome://theme/IDR_COOKIE_ICON");

      dict->SetString(kKeyDomain, node.GetDetailedInfo().flash_lso_domain);
    }
    default:
#if defined(OS_MACOSX)
      dict->SetString(kKeyIcon, "chrome://theme/IDR_BOOKMARK_BAR_FOLDER");
#endif
      break;
  }

  const ExtensionSet* protecting_apps =
      node.GetModel()->ExtensionsProtectingNode(node);
  if (protecting_apps && !protecting_apps->is_empty()) {
    base::ListValue* app_infos = new base::ListValue;
    for (ExtensionSet::const_iterator it = protecting_apps->begin();
         it != protecting_apps->end(); ++it) {
      base::DictionaryValue* app_info = new base::DictionaryValue();
      app_info->SetString(kKeyId, (*it)->id());
      app_info->SetString(kKeyName, (*it)->name());
      app_infos->Append(app_info);
    }
    dict->Set(kKeyAppsProtectingThis, app_infos);
  }

  return true;
}

void CookiesTreeModelUtil::GetChildNodeList(const CookieTreeNode* parent,
                                            int start,
                                            int count,
                                            base::ListValue* nodes) {
  for (int i = 0; i < count; ++i) {
    scoped_ptr<base::DictionaryValue> dict(new DictionaryValue);
    const CookieTreeNode* child = parent->GetChild(start + i);
    if (GetCookieTreeNodeDictionary(*child, dict.get()))
      nodes->Append(dict.release());
  }
}

const CookieTreeNode* CookiesTreeModelUtil::GetTreeNodeFromPath(
    const CookieTreeNode* root,
    const std::string& path) {
  std::vector<std::string> node_ids;
  base::SplitString(path, ',', &node_ids);

  const CookieTreeNode* child = NULL;
  const CookieTreeNode* parent = root;
  int child_index = -1;

  // Validate the tree path and get the node pointer.
  for (size_t i = 0; i < node_ids.size(); ++i) {
    int32 node_id = 0;
    if (!base::StringToInt(node_ids[i], &node_id))
      break;

    child = id_map_.Lookup(node_id);
    child_index = parent->GetIndexOf(child);
    if (child_index == -1)
      break;

    parent = child;
  }

  return child_index >= 0 ? child : NULL;
}
