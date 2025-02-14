// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "android_webview/native/state_serializer.h"

#include <string>

#include "base/memory/scoped_vector.h"
#include "base/pickle.h"
#include "base/time.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"

// Reasons for not re-using TabNavigation under chrome/ as of 20121116:
// * Android WebView has different requirements for fields to store since
//   we are the only ones using values like BaseURLForDataURL.
// * TabNavigation does unnecessary copying of data, which in Android
//   WebView case, is undesired since save/restore is called in Android
//   very frequently.
// * TabNavigation is tightly integrated with the rest of chrome session
//   restore and sync code, and has other purpose in addition to serializing
//   NavigationEntry.

using std::string;

namespace android_webview {

namespace {

// Sanity check value that we are restoring from a valid pickle.
// This can potentially used as an actual serialization version number in the
// future if we ever decide to support restoring from older versions.
const uint32 AW_STATE_VERSION = 20121116;

}  // namespace

bool WriteToPickle(const content::WebContents& web_contents,
                   Pickle* pickle) {
  DCHECK(pickle);

  if (!internal::WriteHeaderToPickle(pickle))
    return false;

  const content::NavigationController& controller =
      web_contents.GetController();
  const int entry_count = controller.GetEntryCount();
  const int selected_entry = controller.GetCurrentEntryIndex();
  DCHECK(entry_count >= 0);
  DCHECK(selected_entry >= -1);  // -1 is valid
  DCHECK(selected_entry < entry_count);

  if (!pickle->WriteInt(entry_count))
    return false;

  if (!pickle->WriteInt(selected_entry))
    return false;

  for (int i = 0; i < entry_count; ++i) {
    if (!internal::WriteNavigationEntryToPickle(*controller.GetEntryAtIndex(i),
                                                pickle))
      return false;
  }

  // Please update AW_STATE_VERSION if serialization format is changed.

  return true;
}

bool RestoreFromPickle(PickleIterator* iterator,
                       content::WebContents* web_contents) {
  DCHECK(iterator);
  DCHECK(web_contents);

  if (!internal::RestoreHeaderFromPickle(iterator))
    return false;

  int entry_count = -1;
  int selected_entry = -2;  // -1 is a valid value

  if (!iterator->ReadInt(&entry_count))
    return false;

  if (!iterator->ReadInt(&selected_entry))
    return false;

  if (entry_count < 0)
    return false;
  if (selected_entry < -1)
    return false;
  if (selected_entry >= entry_count)
    return false;

  ScopedVector<content::NavigationEntry> restored_entries;
  for (int i = 0; i < entry_count; ++i) {
    restored_entries.push_back(content::NavigationEntry::Create());
    if (!internal::RestoreNavigationEntryFromPickle(iterator,
                                                    restored_entries[i]))
      return false;
  }

  // |web_contents| takes ownership of these entries after this call.
  web_contents->GetController().Restore(
      selected_entry,
      content::NavigationController::RESTORE_LAST_SESSION_EXITED_CLEANLY,
      &restored_entries.get());
  DCHECK_EQ(0u, restored_entries.size());

  return true;
}

namespace internal {

bool WriteHeaderToPickle(Pickle* pickle) {
  return pickle->WriteUInt32(AW_STATE_VERSION);
}

bool RestoreHeaderFromPickle(PickleIterator* iterator) {
  uint32 state_version = -1;
  if (!iterator->ReadUInt32(&state_version))
    return false;

  if (AW_STATE_VERSION != state_version)
    return false;

  return true;
}

bool WriteNavigationEntryToPickle(const content::NavigationEntry& entry,
                                  Pickle* pickle) {
  if (!pickle->WriteString(entry.GetURL().spec()))
    return false;

  if (!pickle->WriteString(entry.GetVirtualURL().spec()))
    return false;

  const content::Referrer& referrer = entry.GetReferrer();
  if (!pickle->WriteString(referrer.url.spec()))
    return false;
  if (!pickle->WriteInt(static_cast<int>(referrer.policy)))
    return false;

  if (!pickle->WriteString16(entry.GetTitle()))
    return false;

  if (!pickle->WriteString(entry.GetContentState()))
    return false;

  if (!pickle->WriteBool(static_cast<int>(entry.GetHasPostData())))
    return false;

  if (!pickle->WriteString(entry.GetOriginalRequestURL().spec()))
    return false;

  if (!pickle->WriteBool(static_cast<int>(entry.GetIsOverridingUserAgent())))
    return false;

  if (!pickle->WriteInt64(entry.GetTimestamp().ToInternalValue()))
    return false;

  // Please update AW_STATE_VERSION if serialization format is changed.

  return true;
}

bool RestoreNavigationEntryFromPickle(PickleIterator* iterator,
                                      content::NavigationEntry* entry) {
  {
    string url;
    if (!iterator->ReadString(&url))
      return false;
    entry->SetURL(GURL(url));
  }

  {
    string virtual_url;
    if (!iterator->ReadString(&virtual_url))
      return false;
    entry->SetVirtualURL(GURL(virtual_url));
  }

  {
    content::Referrer referrer;
    string referrer_url;
    int policy;

    if (!iterator->ReadString(&referrer_url))
      return false;
    if (!iterator->ReadInt(&policy))
      return false;

    referrer.url = GURL(referrer_url);
    referrer.policy = static_cast<WebKit::WebReferrerPolicy>(policy);
    entry->SetReferrer(referrer);
  }

  {
    string16 title;
    if (!iterator->ReadString16(&title))
      return false;
    entry->SetTitle(title);
  }

  {
    string content_state;
    if (!iterator->ReadString(&content_state))
      return false;
    entry->SetContentState(content_state);
  }

  {
    bool has_post_data;
    if (!iterator->ReadBool(&has_post_data))
      return false;
    entry->SetHasPostData(has_post_data);
  }

  {
    string original_request_url;
    if (!iterator->ReadString(&original_request_url))
      return false;
    entry->SetOriginalRequestURL(GURL(original_request_url));
  }

  {
    bool is_overriding_user_agent;
    if (!iterator->ReadBool(&is_overriding_user_agent))
      return false;
    entry->SetIsOverridingUserAgent(is_overriding_user_agent);
  }

  {
    int64 timestamp;
    if (!iterator->ReadInt64(&timestamp))
      return false;
    entry->SetTimestamp(base::Time::FromInternalValue(timestamp));
  }

  return true;
}

}  // namespace internal

}  // namespace android_webview
