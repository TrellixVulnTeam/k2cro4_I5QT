// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef  CONTENT_RENDERER_BROWSER_PLUGIN_BROWSER_PLUGIN_H_
#define  CONTENT_RENDERER_BROWSER_PLUGIN_BROWSER_PLUGIN_H_

#include "third_party/WebKit/Source/WebKit/chromium/public/WebPlugin.h"

#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/process_util.h"
#include "base/sequenced_task_runner_helpers.h"
#if defined(OS_WIN)
#include "base/shared_memory.h"
#endif
#include "content/renderer/browser_plugin/browser_plugin_backing_store.h"
#include "content/renderer/browser_plugin/browser_plugin_bindings.h"
#include "content/renderer/render_view_impl.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebDragStatus.h"

struct BrowserPluginHostMsg_AutoSize_Params;
struct BrowserPluginHostMsg_ResizeGuest_Params;
struct BrowserPluginMsg_LoadCommit_Params;
struct BrowserPluginMsg_UpdateRect_Params;

namespace content {

class BrowserPluginManager;
class MockBrowserPlugin;

class CONTENT_EXPORT BrowserPlugin :
    NON_EXPORTED_BASE(public WebKit::WebPlugin) {
 public:
  RenderViewImpl* render_view() const { return render_view_.get(); }
  // Called only by tests to clean up before we blow away the MockRenderProcess.
  void Cleanup();

  // Get the src attribute value of the BrowserPlugin instance.
  std::string src_attribute() const { return src_; }
  // Set the src attribute value of the BrowserPlugin instance.
  bool SetSrcAttribute(const std::string& src, std::string* error_message);
  // Get the autosize attribute value.
  bool auto_size_attribute() const { return auto_size_; }
  // Sets the autosize attribute value.
  void SetAutoSizeAttribute(bool auto_size);
  // Get the maxheight attribute value.
  int max_height_attribute() const { return max_height_; }
  // Set the maxheight attribute value.
  void SetMaxHeightAttribute(int maxheight);
  // Get the maxwidth attribute value.
  int max_width_attribute() const { return max_width_; }
  // Set the maxwidth attribute value.
  void SetMaxWidthAttribute(int max_width);
  // Get the minheight attribute value.
  int min_height_attribute() const { return min_height_; }
  // Set the minheight attribute value.
  void SetMinHeightAttribute(int minheight);
  // Get the minwidth attribute value.
  int min_width_attribute() const { return min_width_; }
  // Set the minwidth attribute value.
  void SetMinWidthAttribute(int minwidth);
  bool InAutoSizeBounds(const gfx::Size& size) const;

  // Get the guest's DOMWindow proxy.
  NPObject* GetContentWindow() const;

  // Returns Chrome's process ID for the current guest.
  int process_id() const { return process_id_; }
  // The partition identifier string is stored as UTF-8.
  std::string GetPartitionAttribute() const;
  // Query whether the guest can navigate back to the previous entry.
  bool CanGoBack() const;
  // Query whether the guest can navigation forward to the next entry.
  bool CanGoForward() const;
  // This method can be successfully called only before the first navigation for
  // this instance of BrowserPlugin. If an error occurs, the |error_message| is
  // set appropriately to indicate the failure reason.
  bool SetPartitionAttribute(const std::string& partition_id,
                             std::string* error_message);

  // Inform the BrowserPlugin to update its backing store with the pixels in
  // its damage buffer.
  void UpdateRect(int message_id,
                  const BrowserPluginMsg_UpdateRect_Params& params);
  // Inform the BrowserPlugin that its guest process is gone.
  void GuestGone(int process_id, base::TerminationStatus status);
  // Inform the BrowserPlugin that the guest has navigated to a new URL.
  void LoadCommit(const BrowserPluginMsg_LoadCommit_Params& params);
  // Inform the BrowserPlugin that the guest has started loading a new page.
  void LoadStart(const GURL& url, bool is_top_level);
  // Inform the BrowserPlugin that the guest has finished loading a new page.
  void LoadStop();
  // Inform the BrowserPlugin that the guest has aborted loading a new page.
  void LoadAbort(const GURL& url, bool is_top_level, const std::string& type);
  // Inform the BrowserPlugin that the guest has redirected a navigation.
  void LoadRedirect(const GURL& old_url,
                    const GURL& new_url,
                    bool is_top_level);
  // Tells the BrowserPlugin to advance the focus to the next (or previous)
  // element.
  void AdvanceFocus(bool reverse);
  // Inform the BrowserPlugin of the focus state of the embedder RenderView.
  void SetEmbedderFocus(bool focused);
  // Informs the guest of an updated focus state.
  void UpdateGuestFocus();
  // Indicates whether the guest should be focused.
  bool ShouldGuestBeFocused() const;

  // Inform the BrowserPlugin that the guest's contentWindow is ready,
  // and provide it with a routing ID to grab it.
  void GuestContentWindowReady(int content_window_routing_id);

  // Informs the BrowserPlugin that the guest has started/stopped accepting
  // touch events.
  void SetAcceptTouchEvents(bool accept);

  // Tells the BrowserPlugin to tell the guest to navigate to the previous
  // navigation entry in the navigation history.
  void Back();
  // Tells the BrowserPlugin to tell the guest to navigate to the next
  // navigation entry in the navigation history.
  void Forward();
  // Tells the BrowserPlugin to tell the guest to navigate to a position
  // relative to the current index in its navigation history.
  void Go(int relativeIndex);
  // Tells the BrowserPlugin to terminate the guest process.
  void TerminateGuest();

  // A request from Javascript has been made to stop the loading of the page.
  void Stop();
  // A request from Javascript has been made to reload the page.
  void Reload();

  // Informs the BrowserPlugin of the cursor that the guest has requested.
  void SetCursor(const WebCursor& cursor);

  bool InBounds(const gfx::Point& point) const;
  gfx::Point ToLocalCoordinates(const gfx::Point& point) const;

  // WebKit::WebPlugin implementation.
  virtual WebKit::WebPluginContainer* container() const OVERRIDE;
  virtual bool initialize(WebKit::WebPluginContainer* container) OVERRIDE;
  virtual void destroy() OVERRIDE;
  virtual NPObject* scriptableObject() OVERRIDE;
  virtual bool supportsKeyboardFocus() const OVERRIDE;
  virtual bool canProcessDrag() const OVERRIDE;
  virtual void paint(
      WebKit::WebCanvas* canvas,
      const WebKit::WebRect& rect) OVERRIDE;
  virtual void updateGeometry(
      const WebKit::WebRect& frame_rect,
      const WebKit::WebRect& clip_rect,
      const WebKit::WebVector<WebKit::WebRect>& cut_outs_rects,
      bool is_visible) OVERRIDE;
  virtual void updateFocus(bool focused) OVERRIDE;
  virtual void updateVisibility(bool visible) OVERRIDE;
  virtual bool acceptsInputEvents() OVERRIDE;
  virtual bool handleInputEvent(
      const WebKit::WebInputEvent& event,
      WebKit::WebCursorInfo& cursor_info) OVERRIDE;
  virtual bool handleDragStatusUpdate(WebKit::WebDragStatus drag_status,
                                      const WebKit::WebDragData& drag_data,
                                      WebKit::WebDragOperationsMask mask,
                                      const WebKit::WebPoint& position,
                                      const WebKit::WebPoint& screen) OVERRIDE;
  virtual void didReceiveResponse(
      const WebKit::WebURLResponse& response) OVERRIDE;
  virtual void didReceiveData(const char* data, int data_length) OVERRIDE;
  virtual void didFinishLoading() OVERRIDE;
  virtual void didFailLoading(const WebKit::WebURLError& error) OVERRIDE;
  virtual void didFinishLoadingFrameRequest(
      const WebKit::WebURL& url,
      void* notify_data) OVERRIDE;
  virtual void didFailLoadingFrameRequest(
      const WebKit::WebURL& url,
      void* notify_data,
      const WebKit::WebURLError& error) OVERRIDE;
 private:
  friend class base::DeleteHelper<BrowserPlugin>;
  // Only the manager is allowed to create a BrowserPlugin.
  friend class BrowserPluginManagerImpl;
  friend class MockBrowserPluginManager;

  // For unit/integration tests.
  friend class MockBrowserPlugin;

  // A BrowserPlugin object is a controller that represents an instance of a
  // browser plugin within the embedder renderer process. Each BrowserPlugin
  // within a process has a unique instance_id that is used to route messages
  // to it. It takes in a RenderViewImpl that it's associated with along
  // with the frame within which it lives and the initial attributes assigned
  // to it on creation.
  BrowserPlugin(
      int instance_id,
      RenderViewImpl* render_view,
      WebKit::WebFrame* frame,
      const WebKit::WebPluginParams& params);

  virtual ~BrowserPlugin();

  int width() const { return plugin_rect_.width(); }
  int height() const { return plugin_rect_.height(); }
  int instance_id() const { return instance_id_; }
  int render_view_routing_id() const { return render_view_routing_id_; }
  BrowserPluginManager* browser_plugin_manager() const {
    return browser_plugin_manager_;
  }

  // Virtual to allow for mocking in tests.
  virtual float GetDeviceScaleFactor() const;

  // Parses the attributes of the browser plugin from the element's attributes
  // and sets them appropriately.
  void ParseAttributes(const WebKit::WebPluginParams& params);

  // Returns the pending resize guest param if there is one. Returns a param
  // with invalid transport dib otherwise.
  BrowserPluginHostMsg_ResizeGuest_Params* GetPendingResizeParams();

  // Initializes the valid events.
  void InitializeEvents();

  // Cleanup event listener state to free v8 resources when a BrowserPlugin
  // is destroyed.
  void RemoveEventListeners();

  // Returns whether |event_name| is a valid event.
  bool IsValidEvent(const std::string& event_name);

  // Triggers the event-listeners for |event_name|. Note that the function
  // frees all the values in |props|.
  void TriggerEvent(const std::string& event_name,
                    std::map<std::string, base::Value*>* props);

  // Creates and maps transport dib. Overridden in tests.
  virtual TransportDIB* CreateTransportDIB(const size_t size);
  // Frees up the damage buffer. Overridden in tests.
  virtual void FreeDamageBuffer();
  // Populates BrowserPluginHostMsg_ResizeGuest_Params with resize state and
  // returns the newly allocated TransportDIB.
  TransportDIB* PopulateResizeGuestParameters(
      BrowserPluginHostMsg_ResizeGuest_Params* params,
      int view_width, int view_height);

  // Populates BrowserPluginHostMsg_AutoSize_Params object with autosize state.
  void PopulateAutoSizeParameters(
      BrowserPluginHostMsg_AutoSize_Params* params);

  // Informs the guest of an updated autosize state.
  void UpdateGuestAutoSizeState();

  // Informs the BrowserPlugin that guest has changed its size in autosize mode.
  void SizeChangedDueToAutoSize(const gfx::Size& old_view_size);

  int instance_id_;
  base::WeakPtr<RenderViewImpl> render_view_;
  // We cache the |render_view_|'s routing ID because we need it on destruction.
  // If the |render_view_| is destroyed before the BrowserPlugin is destroyed
  // then we will attempt to access a NULL pointer.
  int render_view_routing_id_;
  WebKit::WebPluginContainer* container_;
  scoped_ptr<BrowserPluginBindings> bindings_;
  scoped_ptr<BrowserPluginBackingStore> backing_store_;
  TransportDIB* damage_buffer_;
  gfx::Rect plugin_rect_;
  // Bitmap for crashed plugin. Lazily initialized, non-owning pointer.
  SkBitmap* sad_guest_;
  bool guest_crashed_;
  bool resize_pending_;
  scoped_ptr<BrowserPluginHostMsg_ResizeGuest_Params> pending_resize_params_;
  // True if we have ever sent a NavigateGuest message to the embedder.
  bool navigate_src_sent_;
  std::string src_;
  bool auto_size_;
  int max_height_;
  int max_width_;
  int min_height_;
  int min_width_;
  int process_id_;
  std::string storage_partition_id_;
  bool persist_storage_;
  bool valid_partition_id_;
  int content_window_routing_id_;
  bool plugin_focused_;
  bool embedder_focused_;
  // Tracks the visibility of the browser plugin regardless of the whole
  // embedder RenderView's visibility.
  bool visible_;

  WebCursor cursor_;

  gfx::Size last_view_size_;
  bool size_changed_in_flight_;

  // BrowserPlugin outlives RenderViewImpl in Chrome Apps and so we need to
  // store the BrowserPlugin's BrowserPluginManager in a member variable to
  // avoid accessing the RenderViewImpl.
  scoped_refptr<BrowserPluginManager> browser_plugin_manager_;

  // Important: Do not add more history state here.
  // We strongly discourage storing additional history state (such as page IDs)
  // in the embedder process, at the risk of having incorrect information that
  // can lead to broken back/forward logic in apps.
  // It's also important that this state does not get modified by any logic in
  // the embedder process. It should only be updated in response to navigation
  // events in the guest.  No assumptions should be made about how the index
  // will change after a navigation (e.g., for back, forward, or go), because
  // the changes are not always obvious.  For example, there is a maximum
  // number of entries and earlier ones will automatically be pruned.
  int current_nav_entry_index_;
  int nav_entry_count_;
  DISALLOW_COPY_AND_ASSIGN(BrowserPlugin);
};

}  // namespace content

#endif  // CONTENT_RENDERER_BROWSER_PLUGIN_BROWSER_PLUGIN_H_
