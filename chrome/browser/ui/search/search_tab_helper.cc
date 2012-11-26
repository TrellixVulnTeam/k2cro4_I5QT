// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/search/search_tab_helper.h"

#include "chrome/browser/google/google_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/search/search.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/web_contents.h"

DEFINE_WEB_CONTENTS_USER_DATA_KEY(chrome::search::SearchTabHelper)

namespace {

bool IsNTP(const GURL& url) {
  return url.SchemeIs(chrome::kChromeUIScheme) &&
         url.host() == chrome::kChromeUINewTabHost;
}

bool IsSearchEnabled(content::WebContents* web_contents) {
  Profile* profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());
  return chrome::search::IsInstantExtendedAPIEnabled(profile);
}

}  // namespace

namespace chrome {
namespace search {

SearchTabHelper::SearchTabHelper(content::WebContents* web_contents)
    : WebContentsObserver(web_contents),
      is_search_enabled_(IsSearchEnabled(web_contents)),
      is_initial_navigation_commit_(true),
      model_(web_contents) {
  if (!is_search_enabled_)
    return;

  registrar_.Add(
      this,
      content::NOTIFICATION_NAV_ENTRY_COMMITTED,
      content::Source<content::NavigationController>(
          &web_contents->GetController()));
}

SearchTabHelper::~SearchTabHelper() {
}

void SearchTabHelper::OmniboxEditModelChanged(bool user_input_in_progress,
                                              bool cancelling) {
  if (!is_search_enabled_)
    return;

  if (user_input_in_progress) {
    const Mode new_mode = Mode(Mode::MODE_SEARCH_SUGGESTIONS,
                               model_.mode().origin, true);
    model_.SetMode(new_mode);
  } else if (cancelling) {
    UpdateModelBasedOnURL(web_contents()->GetURL(), true);
  }
}

void SearchTabHelper::NavigationEntryUpdated() {
  if (!is_search_enabled_)
    return;
  UpdateModelBasedOnURL(web_contents()->GetURL(), true);
}

void SearchTabHelper::NavigateToPendingEntry(
    const GURL& url,
    content::NavigationController::ReloadType reload_type) {
  if (!is_search_enabled_)
    return;

  // Do not animate if this url is the very first navigation for the tab.
  // NTP mode changes are initiated at "pending", all others are initiated
  // when "committed".  This is because NTP is rendered natively so is faster
  // to render than the web contents and we need to coordinate the animations.
  if (IsNTP(url))
    UpdateModelBasedOnURL(url, !is_initial_navigation_commit_);
}

void SearchTabHelper::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  DCHECK_EQ(content::NOTIFICATION_NAV_ENTRY_COMMITTED, type);
  content::LoadCommittedDetails* committed_details =
      content::Details<content::LoadCommittedDetails>(details).ptr();
  // See comment in |NavigateToPendingEntry()| about why |!IsNTP()| is used.
  if (!IsNTP(committed_details->entry->GetURL())) {
    UpdateModelBasedOnURL(committed_details->entry->GetURL(),
                          !is_initial_navigation_commit_);
  }
  is_initial_navigation_commit_ = false;
}

void SearchTabHelper::UpdateModelBasedOnURL(const GURL& url, bool animate) {
  Mode::Type type = Mode::MODE_DEFAULT;
  Mode::Origin origin = Mode::ORIGIN_DEFAULT;
  if (IsNTP(url)) {
    type = Mode::MODE_NTP;
    origin = Mode::ORIGIN_NTP;
  } else if (google_util::IsInstantExtendedAPIGoogleSearchUrl(url.spec())) {
    type = Mode::MODE_SEARCH_RESULTS;
    origin = Mode::ORIGIN_SEARCH;
  }
  model_.SetMode(Mode(type, origin, animate));
}

const content::WebContents* SearchTabHelper::web_contents() const {
  return model_.web_contents();
}

}  // namespace search
}  // namespace chrome
