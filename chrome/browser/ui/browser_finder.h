// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_BROWSER_FINDER_H_
#define CHROME_BROWSER_UI_BROWSER_FINDER_H_

#include "chrome/browser/ui/browser.h"
#include "ui/gfx/native_widget_types.h"

class Profile;

namespace contents {
class WebContents;
}

// Collection of functions to find Browsers based on various criteria.

namespace browser {

// Deprecated: Use FindTabbedBrowser() instead and pass in a desktop context.
Browser* FindTabbedBrowserDeprecated(Profile* profile,
                                     bool match_original_profiles);

// Retrieve the last active tabbed browser with a profile matching |profile|.
// If |match_original_profiles| is true, matching is done based on the
// original profile, eg profile->GetOriginalProfile() ==
// browser->profile()->GetOriginalProfile(). This has the effect of matching
// against both non-incognito and incognito profiles. If
// |match_original_profiles| is false, only an exact match may be returned.
// |type| refers to the host desktop the returned browser should belong to.
Browser* FindTabbedBrowser(Profile* profile,
                           bool match_original_profiles,
                           chrome::HostDesktopType type);

// Deprecated
Browser* FindOrCreateTabbedBrowser(Profile* profile);

// Returns the first tabbed browser matching |profile|. If there is no tabbed
// browser a new one is created and returned for the desktop specified by
// |type|. If a new browser is created it is not made visible.
Browser* FindOrCreateTabbedBrowser(Profile* profile,
                                   chrome::HostDesktopType type);

// Find an existing browser window with any type. See comment above for
// additional information.
Browser* FindAnyBrowser(Profile* profile, bool match_original_profiles);

// Find an existing browser window with the provided profile and hosted in the
// given desktop. Searches in the order of last activation. Only browsers that
// have been active can be returned. Returns NULL if no such browser currently
// exists.
Browser* FindBrowserWithProfile(Profile* profile,
                                chrome::HostDesktopType type);

// Find an existing browser with the provided ID. Returns NULL if no such
// browser currently exists.
Browser* FindBrowserWithID(SessionID::id_type desired_id);

// Find the browser represented by |window| or NULL if not found.
Browser* FindBrowserWithWindow(gfx::NativeWindow window);

// Find the browser containing |web_contents| or NULL if none is found.
// |web_contents| must not be NULL.
Browser* FindBrowserWithWebContents(const content::WebContents* web_contents);

}  // namespace browser

namespace chrome {

// Returns the Browser object owned by |profile| whose window was most recently
// active. If no such Browsers exist, returns NULL.
//
// WARNING: this is NULL until a browser becomes active. If during startup
// a browser does not become active (perhaps the user launches Chrome, then
// clicks on another app before the first browser window appears) then this
// returns NULL.
// WARNING #2: this will always be NULL in unit tests run on the bots.
// DEPRECATED: DO NOT USE.
Browser* FindLastActiveWithProfile(Profile* profile);

// Returns the Browser object on the given desktop type whose window was most
// recently active. If no such Browsers exist, returns NULL.
//
// WARNING: this is NULL until a browser becomes active. If during startup
// a browser does not become active (perhaps the user launches Chrome, then
// clicks on another app before the first browser window appears) then this
// returns NULL.
// WARNING #2: this will always be NULL in unit tests run on the bots.
Browser* FindLastActiveWithHostDesktopType(HostDesktopType type);

// Returns the number of browsers with the Profile |profile|.
size_t GetBrowserCount(Profile* profile);

// Returns the number of tabbed browsers with the Profile |profile|.
size_t GetTabbedBrowserCount(Profile* profile);

}  // namespace chrome

#endif  // CHROME_BROWSER_UI_BROWSER_FINDER_H_
