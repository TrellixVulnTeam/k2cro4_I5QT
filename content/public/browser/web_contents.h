// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_BROWSER_WEB_CONTENTS_H_
#define CONTENT_PUBLIC_BROWSER_WEB_CONTENTS_H_

#include "base/basictypes.h"
#include "base/callback_forward.h"
#include "base/process_util.h"
#include "base/string16.h"
#include "base/supports_user_data.h"
#include "content/common/content_export.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/save_page_type.h"
#include "content/public/browser/web_ui.h"
#include "ipc/ipc_sender.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/native_widget_types.h"
#include "webkit/glue/window_open_disposition.h"

namespace base {
class TimeTicks;
}

namespace gfx {
class Rect;
class Size;
}

namespace net {
struct LoadStateWithParam;
}

namespace content {

class BrowserContext;
class InterstitialPage;
class RenderProcessHost;
class RenderViewHost;
class RenderWidgetHostView;
class SiteInstance;
class WebContentsDelegate;
class WebContentsView;
struct RendererPreferences;

// Describes what goes in the main content area of a tab.
class WebContents : public PageNavigator,
                    public IPC::Sender,
                    public base::SupportsUserData {
 public:
  // |base_web_contents| is used if we want to size the new WebContents's view
  // based on the view of an existing WebContents.  This can be NULL if not
  // needed.
  CONTENT_EXPORT static WebContents* Create(
      BrowserContext* browser_context,
      SiteInstance* site_instance,
      int routing_id,
      const WebContents* base_web_contents);

  // Similar to Create() above but should be used when you need to prepopulate
  // the SessionStorageNamespaceMap of the WebContents. This can happen if
  // you duplicate a WebContents, try to reconstitute it from a saved state,
  // or when you create a new WebContents based on another one (eg., when
  // servicing a window.open() call).
  //
  // You do not want to call this. If you think you do, make sure you completely
  // understand when SessionStorageNamespace objects should be cloned, why
  // they should not be shared by multiple WebContents, and what bad things
  // can happen if you share the object.
  CONTENT_EXPORT static WebContents* CreateWithSessionStorage(
      BrowserContext* browser_context,
      SiteInstance* site_instance,
      int routing_id,
      const WebContents* base_web_contents,
      const SessionStorageNamespaceMap& session_storage_namespace_map);

  // Returns a WebContents that wraps the RenderViewHost, or NULL if the
  // render view host's delegate isn't a WebContents.
  CONTENT_EXPORT static WebContents* FromRenderViewHost(
      const RenderViewHost* rvh);

  virtual ~WebContents() {}

  // Intrinsic tab state -------------------------------------------------------

  // Gets/Sets the delegate.
  virtual WebContentsDelegate* GetDelegate() = 0;
  virtual void SetDelegate(WebContentsDelegate* delegate) = 0;

  // Gets the controller for this WebContents.
  virtual NavigationController& GetController() = 0;
  virtual const NavigationController& GetController() const = 0;

  // Returns the user browser context associated with this WebContents (via the
  // NavigationController).
  virtual content::BrowserContext* GetBrowserContext() const = 0;

  // Gets the URL that is currently being displayed, if there is one.
  virtual const GURL& GetURL() const = 0;

  // Return the currently active RenderProcessHost and RenderViewHost. Each of
  // these may change over time.
  virtual RenderProcessHost* GetRenderProcessHost() const = 0;

  // Gets the current RenderViewHost for this tab.
  virtual RenderViewHost* GetRenderViewHost() const = 0;

  typedef base::Callback<void(RenderViewHost* /* render_view_host */,
                              int /* x */,
                              int /* y */)> GetRenderViewHostCallback;
  // Gets the RenderViewHost at coordinates (|x|, |y|) for this WebContents via
  // |callback|.
  // This can be different than the current RenderViewHost if there is a
  // BrowserPlugin at the specified position.
  virtual void GetRenderViewHostAtPosition(
      int x,
      int y,
      const GetRenderViewHostCallback& callback) = 0;

  // Gets the current RenderViewHost's routing id. Returns
  // MSG_ROUTING_NONE when there is no RenderViewHost.
  virtual int GetRoutingID() const = 0;

  // Returns the currently active RenderWidgetHostView. This may change over
  // time and can be NULL (during setup and teardown).
  virtual content::RenderWidgetHostView* GetRenderWidgetHostView() const = 0;

  // The WebContentsView will never change and is guaranteed non-NULL.
  virtual WebContentsView* GetView() const = 0;

  // Create a WebUI page for the given url. In most cases, this doesn't need to
  // be called by embedders since content will create its own WebUI objects as
  // necessary. However if the embedder wants to create its own WebUI object and
  // keep track of it manually, it can use this.
  virtual WebUI* CreateWebUI(const GURL& url) = 0;

  // Returns the committed WebUI if one exists, otherwise the pending one.
  // Callers who want to use the pending WebUI for the pending navigation entry
  // should use GetWebUIForCurrentState instead.
  virtual WebUI* GetWebUI() const = 0;
  virtual WebUI* GetCommittedWebUI() const = 0;

  // Allows overriding the user agent used for NavigationEntries it owns.
  virtual void SetUserAgentOverride(const std::string& override) = 0;
  virtual const std::string& GetUserAgentOverride() const = 0;

  // Tab navigation state ------------------------------------------------------

  // Returns the current navigation properties, which if a navigation is
  // pending may be provisional (e.g., the navigation could result in a
  // download, in which case the URL would revert to what it was previously).
  virtual const string16& GetTitle() const = 0;

  // The max page ID for any page that the current SiteInstance has loaded in
  // this WebContents.  Page IDs are specific to a given SiteInstance and
  // WebContents, corresponding to a specific RenderView in the renderer.
  // Page IDs increase with each new page that is loaded by a tab.
  virtual int32 GetMaxPageID() = 0;

  // The max page ID for any page that the given SiteInstance has loaded in
  // this WebContents.
  virtual int32 GetMaxPageIDForSiteInstance(SiteInstance* site_instance) = 0;

  // Returns the SiteInstance associated with the current page.
  virtual SiteInstance* GetSiteInstance() const = 0;

  // Returns the SiteInstance for the pending navigation, if any.  Otherwise
  // returns the current SiteInstance.
  virtual SiteInstance* GetPendingSiteInstance() const = 0;

  // Return whether this WebContents is loading a resource.
  virtual bool IsLoading() const = 0;

  // Returns whether this WebContents is waiting for a first-response for the
  // main resource of the page.
  virtual bool IsWaitingForResponse() const = 0;

  // Return the current load state and the URL associated with it.
  virtual const net::LoadStateWithParam& GetLoadState() const = 0;
  virtual const string16& GetLoadStateHost() const = 0;

  // Return the upload progress.
  virtual uint64 GetUploadSize() const = 0;
  virtual uint64 GetUploadPosition() const = 0;

  // Return the character encoding of the page.
  virtual const std::string& GetEncoding() const = 0;

  // True if this is a secure page which displayed insecure content.
  virtual bool DisplayedInsecureContent() const = 0;

  // Internal state ------------------------------------------------------------

  // This flag indicates whether the WebContents is currently being
  // screenshotted.
  virtual void SetCapturingContents(bool cap) = 0;

  // Indicates whether this tab should be considered crashed. The setter will
  // also notify the delegate when the flag is changed.
  virtual bool IsCrashed() const  = 0;
  virtual void SetIsCrashed(base::TerminationStatus status, int error_code) = 0;

  virtual base::TerminationStatus GetCrashedStatus() const = 0;

  // Whether the tab is in the process of being destroyed.
  virtual bool IsBeingDestroyed() const = 0;

  // Convenience method for notifying the delegate of a navigation state
  // change. See InvalidateType enum.
  virtual void NotifyNavigationStateChanged(unsigned changed_flags) = 0;

  // Get the last time that the WebContents was made visible with WasShown()
  virtual base::TimeTicks GetLastSelectedTime() const = 0;

  // Invoked when the WebContents becomes shown/hidden.
  virtual void WasShown() = 0;
  virtual void WasHidden() = 0;

  // Returns true if the before unload and unload listeners need to be
  // fired. The value of this changes over time. For example, if true and the
  // before unload listener is executed and allows the user to exit, then this
  // returns false.
  virtual bool NeedToFireBeforeUnload() = 0;

  // Commands ------------------------------------------------------------------

  // Stop any pending navigation.
  virtual void Stop() = 0;

  // Creates a new WebContents with the same state as this one. The returned
  // heap-allocated pointer is owned by the caller.
  virtual WebContents* Clone() = 0;

  // Views and focus -----------------------------------------------------------
  // TODO(brettw): Most of these should be removed and the caller should call
  // the view directly.

  // Returns the actual window that is focused when this WebContents is shown.
  virtual gfx::NativeView GetContentNativeView() const = 0;

  // Returns the NativeView associated with this WebContents. Outside of
  // automation in the context of the UI, this is required to be implemented.
  virtual gfx::NativeView GetNativeView() const = 0;

  // Returns the bounds of this WebContents in the screen coordinate system.
  virtual void GetContainerBounds(gfx::Rect* out) const = 0;

  // Makes the tab the focused window.
  virtual void Focus() = 0;

  // Focuses the first (last if |reverse| is true) element in the page.
  // Invoked when this tab is getting the focus through tab traversal (|reverse|
  // is true when using Shift-Tab).
  virtual void FocusThroughTabTraversal(bool reverse) = 0;

  // Interstitials -------------------------------------------------------------

  // Various other systems need to know about our interstitials.
  virtual bool ShowingInterstitialPage() const = 0;

  // Returns the currently showing interstitial, NULL if no interstitial is
  // showing.
  virtual InterstitialPage* GetInterstitialPage() const = 0;

  // Misc state & callbacks ----------------------------------------------------

  // Check whether we can do the saving page operation this page given its MIME
  // type.
  virtual bool IsSavable() = 0;

  // Prepare for saving the current web page to disk.
  virtual void OnSavePage() = 0;

  // Save page with the main HTML file path, the directory for saving resources,
  // and the save type: HTML only or complete web page. Returns true if the
  // saving process has been initiated successfully.
  virtual bool SavePage(const FilePath& main_file,
                        const FilePath& dir_path,
                        SavePageType save_type) = 0;

  // Generate an MHTML representation of the current page in the given file.
  virtual void GenerateMHTML(
      const FilePath& file,
      const base::Callback<void(const FilePath& /* path to the MHTML file */,
                                int64 /* size of the file */)>& callback) = 0;

  // Returns true if the active NavigationEntry's page_id equals page_id.
  virtual bool IsActiveEntry(int32 page_id) = 0;

  // Returns the contents MIME type after a navigation.
  virtual const std::string& GetContentsMimeType() const = 0;

  // Returns true if this WebContents will notify about disconnection.
  virtual bool WillNotifyDisconnection() const = 0;

  // Override the encoding and reload the page by sending down
  // ViewMsg_SetPageEncoding to the renderer. |UpdateEncoding| is kinda
  // the opposite of this, by which 'browser' is notified of
  // the encoding of the current tab from 'renderer' (determined by
  // auto-detect, http header, meta, bom detection, etc).
  virtual void SetOverrideEncoding(const std::string& encoding) = 0;

  // Remove any user-defined override encoding and reload by sending down
  // ViewMsg_ResetPageEncodingToDefault to the renderer.
  virtual void ResetOverrideEncoding() = 0;

  // Returns the settings which get passed to the renderer.
  virtual content::RendererPreferences* GetMutableRendererPrefs() = 0;

  // Set the time when we started to create the new tab page.  This time is
  // from before we created this WebContents.
  virtual void SetNewTabStartTime(const base::TimeTicks& time) = 0;
  virtual base::TimeTicks GetNewTabStartTime() const = 0;

  // Tells the tab to close now. The tab will take care not to close until it's
  // out of nested message loops.
  virtual void Close() = 0;

  // Notification that tab closing has started.  This can be called multiple
  // times, subsequent calls are ignored.
  virtual void OnCloseStarted() = 0;

  // Returns true if underlying WebContentsView should accept drag-n-drop.
  virtual bool ShouldAcceptDragAndDrop() const = 0;

  // A render view-originated drag has ended. Informs the render view host and
  // WebContentsDelegate.
  virtual void SystemDragEnded() = 0;

  // Notification the user has made a gesture while focus was on the
  // page. This is used to avoid uninitiated user downloads (aka carpet
  // bombing), see DownloadRequestLimiter for details.
  virtual void UserGestureDone() = 0;

  // Indicates if this tab was explicitly closed by the user (control-w, close
  // tab menu item...). This is false for actions that indirectly close the tab,
  // such as closing the window.  The setter is maintained by TabStripModel, and
  // the getter only useful from within TAB_CLOSED notification
  virtual void SetClosedByUserGesture(bool value) = 0;
  virtual bool GetClosedByUserGesture() const = 0;

  // Gets the zoom level for this tab.
  virtual double GetZoomLevel() const = 0;

  // Gets the zoom percent for this tab.
  virtual int GetZoomPercent(bool* enable_increment,
                             bool* enable_decrement) const = 0;

  // Opens view-source tab for this contents.
  virtual void ViewSource() = 0;

  virtual void ViewFrameSource(const GURL& url,
                               const std::string& content_state)= 0;

  // Gets the minimum/maximum zoom percent.
  virtual int GetMinimumZoomPercent() const = 0;
  virtual int GetMaximumZoomPercent() const = 0;

  // Gets the preferred size of the contents.
  virtual gfx::Size GetPreferredSize() const = 0;

  // Get the content restrictions (see content::ContentRestriction).
  virtual int GetContentRestrictions() const = 0;

  // Query the WebUIFactory for the TypeID for the current URL.
  virtual WebUI::TypeID GetWebUITypeForCurrentState() = 0;

  // Returns the WebUI for the current state of the tab. This will either be
  // the pending WebUI, the committed WebUI, or NULL.
  virtual WebUI* GetWebUIForCurrentState()= 0;

  // Called when the reponse to a pending mouse lock request has arrived.
  // Returns true if |allowed| is true and the mouse has been successfully
  // locked.
  virtual bool GotResponseToLockMouseRequest(bool allowed) = 0;

  // Called when the user has selected a color in the color chooser.
  virtual void DidChooseColorInColorChooser(int color_chooser_id,
                                            SkColor color) = 0;

  // Called when the color chooser has ended.
  virtual void DidEndColorChooser(int color_chooser_id) = 0;

  // Returns true if the location bar should be focused by default rather than
  // the page contents. The view calls this function when the tab is focused
  // to see what it should do.
  virtual bool FocusLocationBarByDefault() = 0;

  // Focuses the location bar.
  virtual void SetFocusToLocationBar(bool select_all) = 0;

  // Does this have an opener associated with it?
  virtual bool HasOpener() const = 0;
};

}  // namespace content

#endif  // CONTENT_PUBLIC_BROWSER_WEB_CONTENTS_H_
