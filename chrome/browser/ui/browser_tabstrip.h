// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_BROWSER_TABSTRIP_H_
#define CHROME_BROWSER_UI_BROWSER_TABSTRIP_H_

#include "content/public/common/page_transition_types.h"
#include "content/public/browser/navigation_controller.h"
#include "webkit/glue/window_open_disposition.h"

class Browser;
class GURL;
class Profile;
class TabContents;

namespace content {
class SiteInstance;
class WebContents;
}

namespace gfx {
class Rect;
}

namespace chrome {

content::WebContents* GetActiveWebContents(const Browser* browser);

content::WebContents* GetWebContentsAt(const Browser* browser, int index);

void ActivateTabAt(Browser* browser, int index, bool user_gesture);

// Adds a blank tab to the tab strip of the specified browser; an |index| of -1
// means to append it to the end of the tab strip.
void AddBlankTabAt(Browser* browser, int index, bool foreground);

// Adds a selected tab with the specified URL and transition, returns the
// created TabContents.
content::WebContents* AddSelectedTabWithURL(Browser* browser,
                                            const GURL& url,
                                            content::PageTransition transition);

// Creates a new tab with the already-created WebContents 'new_contents'.
// The window for the added contents will be reparented correctly when this
// method returns.  If |disposition| is NEW_POPUP, |pos| should hold the
// initial position. If |was_blocked| is non-NULL, then |*was_blocked| will be
// set to true if the popup gets blocked, and left unchanged otherwise.
void AddWebContents(Browser* browser,
                    content::WebContents* source_contents,
                    content::WebContents* new_contents,
                    WindowOpenDisposition disposition,
                    const gfx::Rect& initial_pos,
                    bool user_gesture,
                    bool* was__blocked);
void CloseWebContents(Browser* browser, content::WebContents* contents);

// Centralized methods for creating a TabContents, configuring and
// installing all its supporting objects and observers.
TabContents* TabContentsFactory(
    Profile* profile,
    content::SiteInstance* site_instance,
    int routing_id,
    const content::WebContents* base_web_contents);

// Same as TabContentsFactory, but allows specifying the initial
// |session_storage_namespace_map|.  This is for supporting session restore
// where we reconstitute the session storage namespaces for a browsing context.
//
// You do not want to call this. If you think you do, make sure you completely
// understand when SessionStorageNamespace objects should be cloned, why
// they should not be shared by multiple WebContents, and what bad things
// can happen if you share the object.
TabContents* TabContentsWithSessionStorageFactory(
    Profile* profile,
    content::SiteInstance* site_instance,
    int routing_id,
    const content::WebContents* base_web_contents,
    const content::SessionStorageNamespaceMap& session_storage_namespace_map);

}  // namespace chrome

#endif  // CHROME_BROWSER_UI_BROWSER_TABSTRIP_H_
