// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/content_settings/content_settings_api.h"

#include <set>
#include <vector>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/memory/scoped_ptr.h"
#include "base/values.h"
#include "chrome/browser/content_settings/cookie_settings.h"
#include "chrome/browser/content_settings/host_content_settings_map.h"
#include "chrome/browser/extensions/api/content_settings/content_settings_api_constants.h"
#include "chrome/browser/extensions/api/content_settings/content_settings_helpers.h"
#include "chrome/browser/extensions/api/content_settings/content_settings_store.h"
#include "chrome/browser/extensions/api/preference/preference_api_constants.h"
#include "chrome/browser/extensions/api/preference/preference_helpers.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/plugins/plugin_finder.h"
#include "chrome/browser/plugins/plugin_installer.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/api/content_settings.h"
#include "content/public/browser/plugin_service.h"
#include "extensions/common/error_utils.h"

using content::BrowserThread;
using content::PluginService;

namespace Clear = extensions::api::content_settings::ContentSetting::Clear;
namespace Get = extensions::api::content_settings::ContentSetting::Get;
namespace Set = extensions::api::content_settings::ContentSetting::Set;
namespace pref_helpers = extensions::preference_helpers;
namespace pref_keys = extensions::preference_api_constants;

namespace {

const std::vector<webkit::WebPluginInfo>* g_testing_plugins_;

bool RemoveContentType(ListValue* args, ContentSettingsType* content_type) {
  std::string content_type_str;
  if (!args->GetString(0, &content_type_str))
    return false;
  // We remove the ContentSettingsType parameter since this is added by the
  // renderer, and is not part of the JSON schema.
  args->Remove(0, NULL);
  *content_type =
      extensions::content_settings_helpers::StringToContentSettingsType(
          content_type_str);
  return *content_type != CONTENT_SETTINGS_TYPE_DEFAULT;
}

}  // namespace

namespace extensions {

namespace helpers = content_settings_helpers;
namespace keys = content_settings_api_constants;

bool ClearContentSettingsFunction::RunImpl() {
  ContentSettingsType content_type;
  EXTENSION_FUNCTION_VALIDATE(RemoveContentType(args_.get(), &content_type));

  scoped_ptr<Clear::Params> params(Clear::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  ExtensionPrefsScope scope = kExtensionPrefsScopeRegular;
  bool incognito = false;
  if (params->details.scope ==
          Clear::Params::Details::SCOPE_INCOGNITO_SESSION_ONLY) {
    scope = kExtensionPrefsScopeIncognitoSessionOnly;
    incognito = true;
  }

  if (incognito) {
    // We don't check incognito permissions here, as an extension should be
    // always allowed to clear its own settings.
  } else {
    // Incognito profiles can't access regular mode ever, they only exist in
    // split mode.
    if (profile()->IsOffTheRecord()) {
      error_ = keys::kIncognitoContextError;
      return false;
    }
  }

  ContentSettingsStore* store =
      profile_->GetExtensionService()->GetContentSettingsStore();
  store->ClearContentSettingsForExtension(extension_id(), scope);

  return true;
}

bool GetContentSettingFunction::RunImpl() {
  ContentSettingsType content_type;
  EXTENSION_FUNCTION_VALIDATE(RemoveContentType(args_.get(), &content_type));

  scoped_ptr<Get::Params> params(Get::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  GURL primary_url(params->details.primary_url);
  if (!primary_url.is_valid()) {
    error_ = ErrorUtils::FormatErrorMessage(keys::kInvalidUrlError,
        params->details.primary_url);
    return false;
  }

  GURL secondary_url(primary_url);
  if (params->details.secondary_url.get()) {
    secondary_url = GURL(*params->details.secondary_url);
    if (!secondary_url.is_valid()) {
      error_ = ErrorUtils::FormatErrorMessage(keys::kInvalidUrlError,
        *params->details.secondary_url);
      return false;
    }
  }

  std::string resource_identifier;
  if (params->details.resource_identifier.get())
    resource_identifier = params->details.resource_identifier->id;

  bool incognito = false;
  if (params->details.incognito.get())
    incognito = *params->details.incognito;
  if (incognito && !include_incognito()) {
    error_ = pref_keys::kIncognitoErrorMessage;
    return false;
  }

  HostContentSettingsMap* map;
  CookieSettings* cookie_settings;
  if (incognito) {
    if (!profile()->HasOffTheRecordProfile()) {
      // TODO(bauerb): Allow reading incognito content settings
      // outside of an incognito session.
      error_ = keys::kIncognitoSessionOnlyError;
      return false;
    }
    map = profile()->GetOffTheRecordProfile()->GetHostContentSettingsMap();
    cookie_settings = CookieSettings::Factory::GetForProfile(
        profile()->GetOffTheRecordProfile());
  } else {
    map = profile()->GetHostContentSettingsMap();
    cookie_settings = CookieSettings::Factory::GetForProfile(profile());
  }

  ContentSetting setting;
  if (content_type == CONTENT_SETTINGS_TYPE_COOKIES) {
    // TODO(jochen): Do we return the value for setting or for reading cookies?
    bool setting_cookie = false;
    setting = cookie_settings->GetCookieSetting(primary_url, secondary_url,
                                                setting_cookie, NULL);
  } else {
    setting = map->GetContentSetting(primary_url, secondary_url, content_type,
                                     resource_identifier);
  }

  DictionaryValue* result = new DictionaryValue();
  result->SetString(keys::kContentSettingKey,
                    helpers::ContentSettingToString(setting));

  SetResult(result);

  return true;
}

bool SetContentSettingFunction::RunImpl() {
  ContentSettingsType content_type;
  EXTENSION_FUNCTION_VALIDATE(RemoveContentType(args_.get(), &content_type));

  scoped_ptr<Set::Params> params(Set::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  std::string primary_error;
  ContentSettingsPattern primary_pattern =
      helpers::ParseExtensionPattern(params->details.primary_pattern,
                                     &primary_error);
  if (!primary_pattern.IsValid()) {
    error_ = primary_error;
    return false;
  }

  ContentSettingsPattern secondary_pattern = ContentSettingsPattern::Wildcard();
  std::string secondary_pattern_str;
  if (params->details.secondary_pattern.get()) {
    std::string secondary_error;
    secondary_pattern =
        helpers::ParseExtensionPattern(*params->details.secondary_pattern,
                                       &secondary_error);
    if (!secondary_pattern.IsValid()) {
      error_ = secondary_error;
      return false;
    }
  }

  std::string resource_identifier;
  if (params->details.resource_identifier.get())
    resource_identifier = params->details.resource_identifier->id;

  std::string setting_str;
  EXTENSION_FUNCTION_VALIDATE(
      params->details.setting.value().GetAsString(&setting_str));
  ContentSetting setting;
  EXTENSION_FUNCTION_VALIDATE(
      helpers::StringToContentSetting(setting_str, &setting));
  EXTENSION_FUNCTION_VALIDATE(
      HostContentSettingsMap::IsSettingAllowedForType(profile()->GetPrefs(),
                                                      setting,
                                                      content_type));

  ExtensionPrefsScope scope = kExtensionPrefsScopeRegular;
  bool incognito = false;
  if (params->details.scope ==
          Set::Params::Details::SCOPE_INCOGNITO_SESSION_ONLY) {
    scope = kExtensionPrefsScopeIncognitoSessionOnly;
    incognito = true;
  }

  if (incognito) {
    // Regular profiles can't access incognito unless include_incognito is true.
    if (!profile()->IsOffTheRecord() && !include_incognito()) {
      error_ = pref_keys::kIncognitoErrorMessage;
      return false;
    }
  } else {
    // Incognito profiles can't access regular mode ever, they only exist in
    // split mode.
    if (profile()->IsOffTheRecord()) {
      error_ = keys::kIncognitoContextError;
      return false;
    }
  }

  if (scope == kExtensionPrefsScopeIncognitoSessionOnly &&
      !profile_->HasOffTheRecordProfile()) {
    error_ = pref_keys::kIncognitoSessionOnlyErrorMessage;
    return false;
  }

  ContentSettingsStore* store =
      profile_->GetExtensionService()->GetContentSettingsStore();
  store->SetExtensionContentSetting(extension_id(), primary_pattern,
                                    secondary_pattern, content_type,
                                    resource_identifier, setting, scope);
  return true;
}

bool GetResourceIdentifiersFunction::RunImpl() {
  ContentSettingsType content_type;
  EXTENSION_FUNCTION_VALIDATE(RemoveContentType(args_.get(), &content_type));

  if (content_type != CONTENT_SETTINGS_TYPE_PLUGINS) {
    SendResponse(true);
    return true;
  }

  if (!g_testing_plugins_) {
    PluginService::GetInstance()->GetPlugins(
        base::Bind(&GetResourceIdentifiersFunction::OnGotPlugins, this));
  } else {
    OnGotPlugins(*g_testing_plugins_);
  }
  return true;
}

void GetResourceIdentifiersFunction::OnGotPlugins(
    const std::vector<webkit::WebPluginInfo>& plugins) {
  PluginFinder* finder = PluginFinder::GetInstance();
  std::set<std::string> group_identifiers;
  ListValue* list = new ListValue();
  for (std::vector<webkit::WebPluginInfo>::const_iterator it = plugins.begin();
       it != plugins.end(); ++it) {
    scoped_ptr<PluginMetadata> plugin_metadata(finder->GetPluginMetadata(*it));
    const std::string& group_identifier = plugin_metadata->identifier();
    if (group_identifiers.find(group_identifier) != group_identifiers.end())
      continue;

    group_identifiers.insert(group_identifier);
    DictionaryValue* dict = new DictionaryValue();
    dict->SetString(keys::kIdKey, group_identifier);
    dict->SetString(keys::kDescriptionKey, plugin_metadata->name());
    list->Append(dict);
  }
  SetResult(list);
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE, base::Bind(
          &GetResourceIdentifiersFunction::SendResponse, this, true));
}

// static
void GetResourceIdentifiersFunction::SetPluginsForTesting(
    const std::vector<webkit::WebPluginInfo>* plugins) {
  g_testing_plugins_ = plugins;
}

}  // namespace extensions
