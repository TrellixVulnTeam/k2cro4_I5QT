// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/browser_plugin/browser_plugin.h"

#include "base/json/json_string_value_serializer.h"
#include "base/message_loop.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "content/common/browser_plugin_messages.h"
#include "content/common/view_messages.h"
#include "content/public/common/content_client.h"
#include "content/public/renderer/content_renderer_client.h"
#include "content/renderer/browser_plugin/browser_plugin_bindings.h"
#include "content/renderer/browser_plugin/browser_plugin_manager.h"
#include "content/renderer/render_process_impl.h"
#include "content/renderer/render_thread_impl.h"
#include "content/renderer/v8_value_converter_impl.h"
#include "skia/ext/platform_canvas.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebBindings.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebDOMCustomEvent.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebDocument.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebElement.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebFrame.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebInputEvent.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebPluginContainer.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebPluginParams.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebRect.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebScriptSource.h"
#include "webkit/plugins/sad_plugin.h"

#if defined (OS_WIN)
#include "base/sys_info.h"
#endif

using WebKit::WebCanvas;
using WebKit::WebPluginContainer;
using WebKit::WebPluginParams;
using WebKit::WebPoint;
using WebKit::WebRect;
using WebKit::WebURL;
using WebKit::WebVector;

namespace content {

namespace {

// Events.
const char kEventExit[] = "exit";
const char kEventLoadAbort[] = "loadabort";
const char kEventLoadCommit[] = "loadcommit";
const char kEventLoadRedirect[] = "loadredirect";
const char kEventLoadStart[] = "loadstart";
const char kEventLoadStop[] = "loadstop";
const char kEventSizeChanged[] = "sizechanged";

// Parameters/properties on events.
const char kIsTopLevel[] = "isTopLevel";
const char kNewURL[] = "newUrl";
const char kNewHeight[] = "newHeight";
const char kNewWidth[] = "newWidth";
const char kOldURL[] = "oldUrl";
const char kOldHeight[] = "oldHeight";
const char kOldWidth[] = "oldWidth";
const char kPartition[] = "partition";
const char kPersistPrefix[] = "persist:";
const char kProcessId[] = "processId";
const char kSrc[] = "src";
const char kReason[] = "reason";
const char kURL[] = "url";

// Error messages.
const char kErrorAlreadyNavigated[] =
    "The object has already navigated, so its partition cannot be changed.";
const char kErrorInvalidPartition[] =
    "Invalid partition attribute.";

static std::string TerminationStatusToString(base::TerminationStatus status) {
  switch (status) {
    case base::TERMINATION_STATUS_NORMAL_TERMINATION:
      return "normal";
    case base::TERMINATION_STATUS_ABNORMAL_TERMINATION:
      return "abnormal";
    case base::TERMINATION_STATUS_PROCESS_WAS_KILLED:
      return "killed";
    case base::TERMINATION_STATUS_PROCESS_CRASHED:
      return "crashed";
    default:
      // This should never happen.
      DCHECK(false);
      return "unknown";
  }
}
}

BrowserPlugin::BrowserPlugin(
    int instance_id,
    RenderViewImpl* render_view,
    WebKit::WebFrame* frame,
    const WebPluginParams& params)
    : instance_id_(instance_id),
      render_view_(render_view->AsWeakPtr()),
      render_view_routing_id_(render_view->GetRoutingID()),
      container_(NULL),
      damage_buffer_(NULL),
      sad_guest_(NULL),
      guest_crashed_(false),
      resize_pending_(false),
      navigate_src_sent_(false),
      auto_size_(false),
      max_height_(0),
      max_width_(0),
      min_height_(0),
      min_width_(0),
      process_id_(-1),
      persist_storage_(false),
      valid_partition_id_(true),
      content_window_routing_id_(MSG_ROUTING_NONE),
      plugin_focused_(false),
      embedder_focused_(false),
      visible_(true),
      size_changed_in_flight_(false),
      browser_plugin_manager_(render_view->browser_plugin_manager()),
      current_nav_entry_index_(0),
      nav_entry_count_(0) {
  browser_plugin_manager()->AddBrowserPlugin(instance_id, this);
  bindings_.reset(new BrowserPluginBindings(this));

  ParseAttributes(params);
}

BrowserPlugin::~BrowserPlugin() {
  if (damage_buffer_)
    FreeDamageBuffer();
  browser_plugin_manager()->RemoveBrowserPlugin(instance_id_);
  browser_plugin_manager()->Send(
      new BrowserPluginHostMsg_PluginDestroyed(
          render_view_routing_id_,
          instance_id_));
}

void BrowserPlugin::Cleanup() {
  if (damage_buffer_)
    FreeDamageBuffer();
}

bool BrowserPlugin::SetSrcAttribute(const std::string& src,
                                    std::string* error_message) {
  if (!valid_partition_id_) {
    *error_message = kErrorInvalidPartition;
    return false;
  }

  if (src.empty() || (src == src_ && !guest_crashed_))
    return true;

  // If we haven't created the guest yet, do so now. We will navigate it right
  // after creation. If |src| is empty, we can delay the creation until we
  // acutally need it.
  if (!navigate_src_sent_) {
    BrowserPluginHostMsg_CreateGuest_Params params;
    params.storage_partition_id = storage_partition_id_;
    params.persist_storage = persist_storage_;
    params.focused = ShouldGuestBeFocused();
    params.visible = visible_;
    PopulateAutoSizeParameters(&params.auto_size);
    browser_plugin_manager()->Send(
        new BrowserPluginHostMsg_CreateGuest(
            render_view_routing_id_,
            instance_id_,
            params));
  }

  scoped_ptr<BrowserPluginHostMsg_ResizeGuest_Params> params(
      GetPendingResizeParams());
  DCHECK(!params->resize_pending);

  browser_plugin_manager()->Send(
      new BrowserPluginHostMsg_NavigateGuest(
          render_view_routing_id_,
          instance_id_,
          src,
          *params));
  // Record that we sent a NavigateGuest message to embedder.
  // Once this instance has navigated, the storage partition cannot be changed,
  // so this value is used for enforcing this.
  navigate_src_sent_ = true;
  src_ = src;
  return true;
}

void BrowserPlugin::SetAutoSizeAttribute(bool auto_size) {
  if (auto_size_ == auto_size)
    return;
  auto_size_ = auto_size;
  last_view_size_ = plugin_rect_.size();
  UpdateGuestAutoSizeState();
}

void BrowserPlugin::PopulateAutoSizeParameters(
    BrowserPluginHostMsg_AutoSize_Params* params) {
  // If maxWidth or maxHeight have not been set, set them to the container size.
  max_height_ = max_height_ ? max_height_ : height();
  max_width_ = max_width_ ? max_width_ : width();
  // minWidth should not be bigger than maxWidth, and minHeight should not be
  // bigger than maxHeight.
  min_height_ = std::min(min_height_, max_height_);
  min_width_ = std::min(min_width_, max_width_);
  params->enable = auto_size_;
  params->max_size = gfx::Size(max_width_, max_height_);
  params->min_size = gfx::Size(min_width_, min_height_);
}

void BrowserPlugin::UpdateGuestAutoSizeState() {
  if (!navigate_src_sent_)
    return;
  BrowserPluginHostMsg_AutoSize_Params auto_size_params;
  PopulateAutoSizeParameters(&auto_size_params);
  BrowserPluginHostMsg_ResizeGuest_Params resize_params;
  int view_width = auto_size_params.max_size.width();
  int view_height = auto_size_params.max_size.height();
  if (!auto_size_params.enable) {
    view_width = width();
    view_height = height();
  }
  TransportDIB* new_damage_buffer =
      PopulateResizeGuestParameters(&resize_params, view_width, view_height);
  // AutoSize initiates a resize so we don't want to issue another resize,
  // we just want to make sure the damage buffer has been updated.
  resize_params.resize_pending = true;
  DCHECK(new_damage_buffer);
  browser_plugin_manager()->Send(new BrowserPluginHostMsg_SetAutoSize(
      render_view_routing_id_,
      instance_id_,
      auto_size_params,
      resize_params));
  if (damage_buffer_)
    FreeDamageBuffer();
  damage_buffer_ = new_damage_buffer;
}

void BrowserPlugin::SizeChangedDueToAutoSize(const gfx::Size& old_view_size) {
  size_changed_in_flight_ = false;

  std::map<std::string, base::Value*> props;
  props[kOldHeight] = base::Value::CreateIntegerValue(old_view_size.height());
  props[kOldWidth] = base::Value::CreateIntegerValue(old_view_size.width());
  props[kNewHeight] = base::Value::CreateIntegerValue(last_view_size_.height());
  props[kNewWidth] = base::Value::CreateIntegerValue(last_view_size_.width());
  TriggerEvent(kEventSizeChanged, &props);
}

void BrowserPlugin::SetMaxHeightAttribute(int max_height) {
  if (max_height_ == max_height)
    return;
  max_height_ = max_height;
  if (!auto_size_)
    return;
  UpdateGuestAutoSizeState();
}

void BrowserPlugin::SetMaxWidthAttribute(int max_width) {
  if (max_width_ == max_width)
    return;
  max_width_ = max_width;
  if (!auto_size_)
    return;
  UpdateGuestAutoSizeState();
}

void BrowserPlugin::SetMinHeightAttribute(int min_height) {
  if (min_height_ == min_height)
    return;
  min_height_ = min_height;
  if (!auto_size_)
     return;
  UpdateGuestAutoSizeState();
}

void BrowserPlugin::SetMinWidthAttribute(int min_width) {
  if (min_width_ == min_width)
    return;
  min_width_ = min_width;
  if (!auto_size_)
     return;
  UpdateGuestAutoSizeState();
}

bool BrowserPlugin::InAutoSizeBounds(const gfx::Size& size) const {
  return size.width() <= max_width_ && size.height() <= max_height_;
}

NPObject* BrowserPlugin::GetContentWindow() const {
  if (content_window_routing_id_ == MSG_ROUTING_NONE)
    return NULL;
  RenderViewImpl* guest_render_view = static_cast<RenderViewImpl*>(
      ChildThread::current()->ResolveRoute(content_window_routing_id_));
  if (!guest_render_view)
    return NULL;
  WebKit::WebFrame* guest_frame = guest_render_view->GetWebView()->mainFrame();
  return guest_frame->windowObject();
}

std::string BrowserPlugin::GetPartitionAttribute() const {
  std::string value;
  if (persist_storage_)
    value.append(kPersistPrefix);

  value.append(storage_partition_id_);
  return value;
}

bool BrowserPlugin::CanGoBack() const {
  return nav_entry_count_ > 1 && current_nav_entry_index_ > 0;
}

bool BrowserPlugin::CanGoForward() const {
  return current_nav_entry_index_ >= 0 &&
      current_nav_entry_index_ < (nav_entry_count_ - 1);
}

bool BrowserPlugin::SetPartitionAttribute(const std::string& partition_id,
                                          std::string* error_message) {
  if (navigate_src_sent_) {
    *error_message = kErrorAlreadyNavigated;
    return false;
  }

  std::string input = partition_id;

  // Since the "persist:" prefix is in ASCII, StartsWith will work fine on
  // UTF-8 encoded |partition_id|. If the prefix is a match, we can safely
  // remove the prefix without splicing in the middle of a multi-byte codepoint.
  // We can use the rest of the string as UTF-8 encoded one.
  if (StartsWithASCII(input, kPersistPrefix, true)) {
    size_t index = input.find(":");
    CHECK(index != std::string::npos);
    // It is safe to do index + 1, since we tested for the full prefix above.
    input = input.substr(index + 1);
    if (input.empty()) {
      valid_partition_id_ = false;
      *error_message = kErrorInvalidPartition;
      return false;
    }
    persist_storage_ = true;
  } else {
    persist_storage_ = false;
  }

  valid_partition_id_ = true;
  storage_partition_id_ = input;
  return true;
}

void BrowserPlugin::ParseAttributes(const WebKit::WebPluginParams& params) {
  std::string src;

  // Get the src attribute from the attributes vector
  for (unsigned i = 0; i < params.attributeNames.size(); ++i) {
    std::string attributeName = params.attributeNames[i].utf8();
    if (LowerCaseEqualsASCII(attributeName, kSrc)) {
      src = params.attributeValues[i].utf8();
    } else if (LowerCaseEqualsASCII(attributeName, kPartition)) {
      std::string error;
      SetPartitionAttribute(params.attributeValues[i].utf8(), &error);
    }
  }

  // Set the 'src' attribute last, as it will set the has_navigated_ flag to
  // true, which prevents changing the 'partition' attribute.
  std::string error;
  SetSrcAttribute(src, &error);
}

float BrowserPlugin::GetDeviceScaleFactor() const {
  if (!render_view_)
    return 1.0f;
  return render_view_->GetWebView()->deviceScaleFactor();
}

void BrowserPlugin::TriggerEvent(const std::string& event_name,
                                 std::map<std::string, base::Value*>* props) {
  if (!container() || !container()->element().document().frame())
    return;
  v8::HandleScope handle_scope;
  std::string json_string;
  if (props) {
    base::DictionaryValue dict;
    for (std::map<std::string, base::Value*>::iterator iter = props->begin(),
             end = props->end(); iter != end; ++iter) {
      dict.Set(iter->first, iter->second);
    }

    JSONStringValueSerializer serializer(&json_string);
    if (!serializer.Serialize(dict))
      return;
  }

  WebKit::WebFrame* frame = container()->element().document().frame();
  WebKit::WebDOMEvent dom_event = frame->document().createEvent("CustomEvent");
  WebKit::WebDOMCustomEvent event = dom_event.to<WebKit::WebDOMCustomEvent>();

  // The events triggered directly from the plugin <object> are internal events
  // whose implementation details can (and likely will) change over time. The
  // wrapper/shim (e.g. <webview> tag) should receive these events, and expose a
  // more appropriate (and stable) event to the consumers as part of the API.
  std::string internal_name = base::StringPrintf("-internal-%s",
                                                 event_name.c_str());
  event.initCustomEvent(
      WebKit::WebString::fromUTF8(internal_name.c_str()),
      false, false,
      WebKit::WebSerializedScriptValue::serialize(
          v8::String::New(json_string.c_str(), json_string.size())));
  container()->element().dispatchEvent(event);
}

void BrowserPlugin::Back() {
  if (!navigate_src_sent_)
    return;
  browser_plugin_manager()->Send(
      new BrowserPluginHostMsg_Go(render_view_routing_id_,
                                  instance_id_, -1));
}

void BrowserPlugin::Forward() {
  if (!navigate_src_sent_)
    return;
  browser_plugin_manager()->Send(
      new BrowserPluginHostMsg_Go(render_view_routing_id_,
                                  instance_id_, 1));
}

void BrowserPlugin::Go(int relative_index) {
  if (!navigate_src_sent_)
    return;
  browser_plugin_manager()->Send(
      new BrowserPluginHostMsg_Go(render_view_routing_id_,
                                  instance_id_,
                                  relative_index));
}

void BrowserPlugin::TerminateGuest() {
  if (!navigate_src_sent_)
    return;
  browser_plugin_manager()->Send(
      new BrowserPluginHostMsg_TerminateGuest(render_view_routing_id_,
                                              instance_id_));
}

void BrowserPlugin::Stop() {
  if (!navigate_src_sent_)
    return;
  browser_plugin_manager()->Send(
      new BrowserPluginHostMsg_Stop(render_view_routing_id_,
                                    instance_id_));
}

void BrowserPlugin::Reload() {
  if (!navigate_src_sent_)
    return;
  browser_plugin_manager()->Send(
      new BrowserPluginHostMsg_Reload(render_view_routing_id_,
                                      instance_id_));
}

void BrowserPlugin::SetCursor(const WebCursor& cursor) {
  cursor_ = cursor;
}

void BrowserPlugin::UpdateRect(
    int message_id,
    const BrowserPluginMsg_UpdateRect_Params& params) {
  if ((!auto_size_ &&
       (width() != params.view_size.width() ||
        height() != params.view_size.height())) ||
      (auto_size_ && (!InAutoSizeBounds(params.view_size)))) {
    browser_plugin_manager()->Send(new BrowserPluginHostMsg_UpdateRect_ACK(
        render_view_routing_id_,
        instance_id_,
        message_id,
        gfx::Size(width(), height())));
    return;
  }
  // If the view size has changed since we last updated.
  if (auto_size_ && (params.view_size != last_view_size_)) {
    if (backing_store_)
      backing_store_->Clear(SK_ColorWHITE);
    gfx::Size old_view_size = last_view_size_;
    last_view_size_ = params.view_size;
    // Schedule a SizeChanged instead of calling it directly to ensure that
    // the backing store has been updated before the developer attempts to
    // resize to avoid flicker. |size_changed_in_flight_| acts as a form of
    // flow control for SizeChanged events. If the guest's view size is changing
    // rapidly before a SizeChanged event fires, then we avoid scheduling
    // another SizedChanged event. SizedChanged reads the new size from
    // |last_view_size_| so we can be sure that it always fires an event
    // with the last seen view size.
    if (container_ && !size_changed_in_flight_) {
      size_changed_in_flight_ = true;
      MessageLoop::current()->PostTask(
          FROM_HERE,
          base::Bind(&BrowserPlugin::SizeChangedDueToAutoSize,
                     base::Unretained(this),
                     old_view_size));
   }
  }

  float backing_store_scale_factor =
      backing_store_.get() ? backing_store_->GetScaleFactor() : 1.0f;

  if (!backing_store_ || params.is_resize_ack ||
      (backing_store_scale_factor != params.scale_factor) ||
      params.view_size.width() > backing_store_->GetSize().width() ||
      params.view_size.height() > backing_store_->GetSize().height()) {
    int backing_store_width = auto_size_ ? max_width_ : width();
    int backing_store_height = auto_size_ ? max_height_: height();
    resize_pending_ = !params.is_resize_ack;
    backing_store_.reset(
        new BrowserPluginBackingStore(
            gfx::Size(backing_store_width, backing_store_height),
            params.scale_factor));
  }

  // Update the backing store.
  if (!params.scroll_rect.IsEmpty()) {
    backing_store_->ScrollBackingStore(params.scroll_delta,
                                       params.scroll_rect,
                                       params.view_size);
  }
  for (unsigned i = 0; i < params.copy_rects.size(); i++) {
    backing_store_->PaintToBackingStore(params.bitmap_rect,
                                        params.copy_rects,
                                        damage_buffer_);
  }
  // Invalidate the container.
  // If the BrowserPlugin is scheduled to be deleted, then container_ will be
  // NULL so we shouldn't attempt to access it.
  if (container_)
    container_->invalidate();
  browser_plugin_manager()->Send(new BrowserPluginHostMsg_UpdateRect_ACK(
      render_view_routing_id_,
      instance_id_,
      message_id,
      gfx::Size()));
}

void BrowserPlugin::GuestGone(int process_id, base::TerminationStatus status) {
  // We fire the event listeners before painting the sad graphic to give the
  // developer an opportunity to display an alternative overlay image on crash.
  std::string termination_status = TerminationStatusToString(status);
  std::map<std::string, base::Value*> props;
  props[kProcessId] = base::Value::CreateIntegerValue(process_id);
  props[kReason] = base::Value::CreateStringValue(termination_status);

  // Event listeners may remove the BrowserPlugin from the document. If that
  // happens, the BrowserPlugin will be scheduled for later deletion (see
  // BrowserPlugin::destroy()). That will clear the container_ reference,
  // but leave other member variables valid below.
  TriggerEvent(kEventExit, &props);

  guest_crashed_ = true;
  // We won't paint the contents of the current backing store again so we might
  // as well toss it out and save memory.
  backing_store_.reset();
  // If the BrowserPlugin is scheduled to be deleted, then container_ will be
  // NULL so we shouldn't attempt to access it.
  if (container_)
    container_->invalidate();
}

void BrowserPlugin::LoadStart(const GURL& url, bool is_top_level) {
  std::map<std::string, base::Value*> props;
  props[kURL] = base::Value::CreateStringValue(url.spec());
  props[kIsTopLevel] = base::Value::CreateBooleanValue(is_top_level);

  TriggerEvent(kEventLoadStart, &props);
}

void BrowserPlugin::LoadCommit(
    const BrowserPluginMsg_LoadCommit_Params& params) {
  // If the guest has just committed a new navigation then it is no longer
  // crashed.
  guest_crashed_ = false;
  src_ = params.url.spec();
  process_id_ = params.process_id;
  current_nav_entry_index_ = params.current_entry_index;
  nav_entry_count_ = params.entry_count;

  std::map<std::string, base::Value*> props;
  props[kURL] = base::Value::CreateStringValue(src_);
  props[kIsTopLevel] = base::Value::CreateBooleanValue(params.is_top_level);
  TriggerEvent(kEventLoadCommit, &props);
}

void BrowserPlugin::LoadStop() {
  // Construct the loadStop event object.
  TriggerEvent(kEventLoadStop, NULL);
}

void BrowserPlugin::LoadAbort(const GURL& url,
                              bool is_top_level,
                              const std::string& type) {
  std::map<std::string, base::Value*> props;
  props[kURL] = base::Value::CreateStringValue(url.spec());
  props[kIsTopLevel] = base::Value::CreateBooleanValue(is_top_level);
  props[kReason] = base::Value::CreateStringValue(type);
  TriggerEvent(kEventLoadAbort, &props);
}

void BrowserPlugin::LoadRedirect(const GURL& old_url,
                                 const GURL& new_url,
                                 bool is_top_level) {
  std::map<std::string, base::Value*> props;
  props[kOldURL] = base::Value::CreateStringValue(old_url.spec());
  props[kNewURL] = base::Value::CreateStringValue(new_url.spec());
  props[kIsTopLevel] = base::Value::CreateBooleanValue(is_top_level);
  TriggerEvent(kEventLoadRedirect, &props);
}

void BrowserPlugin::AdvanceFocus(bool reverse) {
  DCHECK(render_view_);
  render_view_->GetWebView()->advanceFocus(reverse);
}

void BrowserPlugin::SetEmbedderFocus(bool focused) {
  if (embedder_focused_ == focused)
    return;

  bool old_guest_focus_state = ShouldGuestBeFocused();
  embedder_focused_ = focused;

  if (ShouldGuestBeFocused() != old_guest_focus_state)
    UpdateGuestFocus();
}

void BrowserPlugin::UpdateGuestFocus() {
  if (!navigate_src_sent_)
    return;
  bool should_be_focused = ShouldGuestBeFocused();
  browser_plugin_manager()->Send(new BrowserPluginHostMsg_SetFocus(
      render_view_routing_id_,
      instance_id_,
      should_be_focused));
}

bool BrowserPlugin::ShouldGuestBeFocused() const {
  return plugin_focused_ && embedder_focused_;
}

void BrowserPlugin::GuestContentWindowReady(int content_window_routing_id) {
  DCHECK(content_window_routing_id != MSG_ROUTING_NONE);
  content_window_routing_id_ = content_window_routing_id;
}

void BrowserPlugin::SetAcceptTouchEvents(bool accept) {
  if (container())
    container()->setIsAcceptingTouchEvents(accept);
}

WebKit::WebPluginContainer* BrowserPlugin::container() const {
  return container_;
}

bool BrowserPlugin::initialize(WebPluginContainer* container) {
  container_ = container;
  return true;
}

void BrowserPlugin::destroy() {
  // The BrowserPlugin's WebPluginContainer is deleted immediately after this
  // call returns, so let's not keep a reference to it around.
  container_ = NULL;
  MessageLoop::current()->DeleteSoon(FROM_HERE, this);
}

NPObject* BrowserPlugin::scriptableObject() {
  NPObject* browser_plugin_np_object(bindings_->np_object());
  // The object is expected to be retained before it is returned.
  WebKit::WebBindings::retainObject(browser_plugin_np_object);
  return browser_plugin_np_object;
}

bool BrowserPlugin::supportsKeyboardFocus() const {
  return true;
}

bool BrowserPlugin::canProcessDrag() const {
  return true;
}

void BrowserPlugin::paint(WebCanvas* canvas, const WebRect& rect) {
  if (guest_crashed_) {
    if (!sad_guest_)  // Lazily initialize bitmap.
      sad_guest_ = content::GetContentClient()->renderer()->
          GetSadWebViewBitmap();
    // content_shell does not have the sad plugin bitmap, so we'll paint black
    // instead to make it clear that something went wrong.
    if (sad_guest_) {
      webkit::PaintSadPlugin(canvas, plugin_rect_, *sad_guest_);
      return;
    }
  }
  SkAutoCanvasRestore auto_restore(canvas, true);
  canvas->translate(plugin_rect_.x(), plugin_rect_.y());
  SkRect image_data_rect = SkRect::MakeXYWH(
      SkIntToScalar(0),
      SkIntToScalar(0),
      SkIntToScalar(plugin_rect_.width()),
      SkIntToScalar(plugin_rect_.height()));
  canvas->clipRect(image_data_rect);
  // Paint black or white in case we have nothing in our backing store or we
  // need to show a gutter.
  SkPaint paint;
  paint.setStyle(SkPaint::kFill_Style);
  paint.setColor(guest_crashed_ ? SK_ColorBLACK : SK_ColorWHITE);
  canvas->drawRect(image_data_rect, paint);
  // Stay a solid color if we have never set a non-empty src, or we don't have a
  // backing store.
  if (!backing_store_.get() || !navigate_src_sent_)
    return;
  float inverse_scale_factor =  1.0f / backing_store_->GetScaleFactor();
  canvas->scale(inverse_scale_factor, inverse_scale_factor);
  canvas->drawBitmap(backing_store_->GetBitmap(), 0, 0);
}

bool BrowserPlugin::InBounds(const gfx::Point& position) const {
  bool result = position.x() >= plugin_rect_.x() &&
      position.x() < plugin_rect_.x() + plugin_rect_.width() &&
      position.y() >= plugin_rect_.y() &&
      position.y() < plugin_rect_.y() + plugin_rect_.height();
  return result;
}

gfx::Point BrowserPlugin::ToLocalCoordinates(const gfx::Point& point) const {
  // TODO(lazyboy): Css transformations? http://crbug.com/160350.
  return gfx::Point(point.x() - plugin_rect_.x(), point.y() - plugin_rect_.y());
}

void BrowserPlugin::updateGeometry(
    const WebRect& window_rect,
    const WebRect& clip_rect,
    const WebVector<WebRect>& cut_outs_rects,
    bool is_visible) {
  int old_width = width();
  int old_height = height();
  plugin_rect_ = window_rect;
  if (auto_size_ || (old_width == window_rect.width &&
                     old_height == window_rect.height)) {
    return;
  }
  pending_resize_params_.reset();

  scoped_ptr<BrowserPluginHostMsg_ResizeGuest_Params> params(
      new BrowserPluginHostMsg_ResizeGuest_Params);

  TransportDIB* new_damage_buffer =
      PopulateResizeGuestParameters(params.get(), width(), height());
  DCHECK(new_damage_buffer);

  if (navigate_src_sent_) {
    browser_plugin_manager()->Send(new BrowserPluginHostMsg_ResizeGuest(
        render_view_routing_id_,
        instance_id_,
        *params));
    resize_pending_ = true;
  } else {
    // Until an actual navigation occurs, there is no browser-side embedder
    // present to notify about geometry updates. In this case, after we've
    // updated the BrowserPlugin's state we are done and we do not send a resize
    // message to the browser.
    pending_resize_params_.reset(params.release());
  }
  if (damage_buffer_)
    FreeDamageBuffer();
  damage_buffer_ = new_damage_buffer;
}

void BrowserPlugin::FreeDamageBuffer() {
  DCHECK(damage_buffer_);
#if defined(OS_MACOSX)
  // We don't need to (nor should we) send ViewHostMsg_FreeTransportDIB
  // message to the browser to free the damage buffer since we manage the
  // damage buffer ourselves.
  delete damage_buffer_;
#else
  RenderProcess::current()->FreeTransportDIB(damage_buffer_);
  damage_buffer_ = NULL;
#endif
}

TransportDIB* BrowserPlugin::PopulateResizeGuestParameters(
    BrowserPluginHostMsg_ResizeGuest_Params* params,
    int view_width, int view_height) {
  const size_t stride = skia::PlatformCanvasStrideForWidth(view_width);
  // Make sure the size of the damage buffer is at least four bytes so that we
  // can fit in a magic word to verify that the memory is shared correctly.
  size_t size =
      std::max(sizeof(unsigned int),
               static_cast<size_t>(view_height *
                                   stride *
                                   GetDeviceScaleFactor() *
                                   GetDeviceScaleFactor()));

  // Don't drop the old damage buffer until after we've made sure that the
  // browser process has dropped it.
  TransportDIB* new_damage_buffer = CreateTransportDIB(size);
  params->damage_buffer_id = new_damage_buffer->id();
#if defined(OS_MACOSX)
  // |damage_buffer_id| is not enough to retrieve the damage buffer (on browser
  // side) since we don't let the browser cache the damage buffer. We need a
  // handle to the damage buffer for this.
  params->damage_buffer_handle = new_damage_buffer->handle();
#endif
#if defined(OS_WIN)
  params->damage_buffer_size = size;
#endif
  params->width = view_width;
  params->height = view_height;
  params->resize_pending = resize_pending_;
  params->scale_factor = GetDeviceScaleFactor();
  return new_damage_buffer;
}

BrowserPluginHostMsg_ResizeGuest_Params*
    BrowserPlugin::GetPendingResizeParams() {
  if (pending_resize_params_.get()) {
    resize_pending_ = true;
    return pending_resize_params_.release();
  } else {
    BrowserPluginHostMsg_ResizeGuest_Params* params =
        new BrowserPluginHostMsg_ResizeGuest_Params;

    // We don't have a pending resize to send, so we send an invalid transport
    // dib Id.
    params->damage_buffer_id = TransportDIB::Id();
    params->width = width();
    params->height = height();
    params->resize_pending = false;
    return params;
  }
}

TransportDIB* BrowserPlugin::CreateTransportDIB(const size_t size) {
#if defined(OS_MACOSX)
  TransportDIB::Handle handle;
  // On OSX we don't let the browser manage the transport dib. We manage the
  // deletion of the dib in FreeDamageBuffer().
  IPC::Message* msg = new ViewHostMsg_AllocTransportDIB(
      size,
      false,  // cache in browser.
      &handle);
  TransportDIB* new_damage_buffer = NULL;
  if (browser_plugin_manager()->Send(msg) && handle.fd >= 0)
    new_damage_buffer = TransportDIB::Map(handle);
#else
  TransportDIB* new_damage_buffer =
      RenderProcess::current()->CreateTransportDIB(size);
#endif
  if (!new_damage_buffer)
    NOTREACHED() << "Unable to create damage buffer";
#if defined(OS_WIN)
  // Windows does not map the buffer by default.
  CHECK(new_damage_buffer->Map());
#endif
  DCHECK(new_damage_buffer->memory());
  // Insert the magic word.
  *static_cast<unsigned int*>(new_damage_buffer->memory()) = 0xdeadbeef;
  return new_damage_buffer;
}

void BrowserPlugin::updateFocus(bool focused) {
  if (plugin_focused_ == focused)
    return;

  bool old_guest_focus_state = ShouldGuestBeFocused();
  plugin_focused_ = focused;

  if (ShouldGuestBeFocused() != old_guest_focus_state)
    UpdateGuestFocus();
}

void BrowserPlugin::updateVisibility(bool visible) {
  if (visible_ == visible)
    return;

  visible_ = visible;
  if (!navigate_src_sent_)
    return;

  browser_plugin_manager()->Send(new BrowserPluginHostMsg_SetVisibility(
      render_view_routing_id_,
      instance_id_,
      visible));
}

bool BrowserPlugin::acceptsInputEvents() {
  return true;
}

bool BrowserPlugin::handleInputEvent(const WebKit::WebInputEvent& event,
                                     WebKit::WebCursorInfo& cursor_info) {
  if (guest_crashed_ || !navigate_src_sent_)
    return false;
  bool handled = false;
  IPC::Message* message =
      new BrowserPluginHostMsg_HandleInputEvent(
          render_view_routing_id_,
          &handled);
  message->WriteInt(instance_id_);
  message->WriteData(reinterpret_cast<const char*>(&plugin_rect_),
                     sizeof(gfx::Rect));
  message->WriteData(reinterpret_cast<const char*>(&event), event.size);
  browser_plugin_manager()->Send(message);
  cursor_.GetCursorInfo(&cursor_info);
  return handled;
}

bool BrowserPlugin::handleDragStatusUpdate(WebKit::WebDragStatus drag_status,
                                           const WebKit::WebDragData& drag_data,
                                           WebKit::WebDragOperationsMask mask,
                                           const WebKit::WebPoint& position,
                                           const WebKit::WebPoint& screen) {
  if (guest_crashed_ || !navigate_src_sent_)
    return false;
  browser_plugin_manager()->Send(
      new BrowserPluginHostMsg_DragStatusUpdate(
        render_view_routing_id_,
        instance_id_,
        drag_status,
        WebDropData(drag_data),
        mask,
        position));
  return false;
}

void BrowserPlugin::didReceiveResponse(
    const WebKit::WebURLResponse& response) {
}

void BrowserPlugin::didReceiveData(const char* data, int data_length) {
}

void BrowserPlugin::didFinishLoading() {
}

void BrowserPlugin::didFailLoading(const WebKit::WebURLError& error) {
}

void BrowserPlugin::didFinishLoadingFrameRequest(const WebKit::WebURL& url,
                                                 void* notify_data) {
}

void BrowserPlugin::didFailLoadingFrameRequest(
    const WebKit::WebURL& url,
    void* notify_data,
    const WebKit::WebURLError& error) {
}

}  // namespace content
