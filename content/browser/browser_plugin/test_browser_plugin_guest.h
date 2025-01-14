// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_BROWSER_PLUGIN_TEST_BROWSER_PLUGIN_GUEST_H_
#define CONTENT_BROWSER_BROWSER_PLUGIN_TEST_BROWSER_PLUGIN_GUEST_H_

#include "base/compiler_specific.h"
#include "base/process_util.h"
#include "content/browser/browser_plugin/browser_plugin_guest.h"
#include "content/public/test/test_utils.h"
#include "ui/gfx/size.h"

namespace content {

class RenderProcessHost;
class RenderViewHost;
class WebContentsImpl;

// Test class for BrowserPluginGuest.
//
// Provides utilities to wait for certain state/messages in BrowserPluginGuest
// to be used in tests.
class TestBrowserPluginGuest : public BrowserPluginGuest {
 public:
  TestBrowserPluginGuest(int instance_id,
                         WebContentsImpl* web_contents,
                         RenderViewHost* render_view_host,
                         const BrowserPluginHostMsg_CreateGuest_Params& params);
  virtual ~TestBrowserPluginGuest();

  WebContentsImpl* web_contents() const;

  // NotificationObserver method override.
  virtual void Observe(int type,
                       const NotificationSource& source,
                       const NotificationDetails& details) OVERRIDE;

  // Overridden methods from BrowserPluginGuest to intercept in test objects.
  virtual void RenderViewGone(base::TerminationStatus status) OVERRIDE;
  virtual void HandleInputEvent(RenderViewHost* render_view_host,
                                const gfx::Rect& guest_rect,
                                const WebKit::WebInputEvent& event,
                                IPC::Message* reply_message) OVERRIDE;
  virtual void SetFocus(bool focused) OVERRIDE;
  virtual bool ViewTakeFocus(bool reverse) OVERRIDE;
  virtual void Reload() OVERRIDE;
  virtual void Stop() OVERRIDE;
  virtual void SetDamageBuffer(TransportDIB* damage_buffer,
#if defined(OS_WIN)
                               int damage_buffer_size,
#endif
                               const gfx::Size& damage_view_size,
                               float scale_factor) OVERRIDE;
  virtual void DidStopLoading(RenderViewHost* render_view_host) OVERRIDE;

  // Test utilities to wait for a event we are interested in.
  // Waits until UpdateRect message is sent from the guest, meaning it is
  // ready/rendered.
  void WaitForUpdateRectMsg();
  void ResetUpdateRectCount();
  // Waits until a guest receives a damage buffer of the specified |size|.
  void WaitForDamageBufferWithSize(const gfx::Size& size);
  // Waits for focus to reach this guest.
  void WaitForFocus();
  // Waits for blur to reach this guest.
  void WaitForBlur();
  // Waits for focus to move out of this guest.
  void WaitForAdvanceFocus();
  // Waits until the guest is hidden.
  void WaitUntilHidden();
  // Waits until guest exits.
  void WaitForExit();
  // Waits until a reload request is observed.
  void WaitForReload();
  // Waits until a stop request is observed.
  void WaitForStop();
  // Waits until input is observed.
  void WaitForInput();
  // Waits until 'loadstop' is observed.
  void WaitForLoadStop();
  // Waits until UpdateRect with a particular |view_size| is observed.
  void WaitForViewSize(const gfx::Size& view_size);

 private:
  // Overridden methods from BrowserPluginGuest to intercept in test objects.
  virtual void SendMessageToEmbedder(IPC::Message* msg) OVERRIDE;

  int update_rect_count_;
  int damage_buffer_call_count_;
  bool exit_observed_;
  bool focus_observed_;
  bool blur_observed_;
  bool advance_focus_observed_;
  bool was_hidden_observed_;
  bool stop_observed_;
  bool reload_observed_;
  bool set_damage_buffer_observed_;
  bool input_observed_;
  bool load_stop_observed_;
  gfx::Size last_view_size_observed_;
  gfx::Size expected_auto_view_size_;

  // For WaitForDamageBufferWithSize().
  bool waiting_for_damage_buffer_with_size_;
  gfx::Size expected_damage_buffer_size_;
  gfx::Size last_damage_buffer_size_;

  scoped_refptr<MessageLoopRunner> send_message_loop_runner_;
  scoped_refptr<MessageLoopRunner> crash_message_loop_runner_;
  scoped_refptr<MessageLoopRunner> focus_message_loop_runner_;
  scoped_refptr<MessageLoopRunner> blur_message_loop_runner_;
  scoped_refptr<MessageLoopRunner> advance_focus_message_loop_runner_;
  scoped_refptr<MessageLoopRunner> was_hidden_message_loop_runner_;
  scoped_refptr<MessageLoopRunner> reload_message_loop_runner_;
  scoped_refptr<MessageLoopRunner> stop_message_loop_runner_;
  scoped_refptr<MessageLoopRunner> damage_buffer_message_loop_runner_;
  scoped_refptr<MessageLoopRunner> input_message_loop_runner_;
  scoped_refptr<MessageLoopRunner> load_stop_message_loop_runner_;
  scoped_refptr<MessageLoopRunner> auto_view_size_message_loop_runner_;

  DISALLOW_COPY_AND_ASSIGN(TestBrowserPluginGuest);
};

}  // namespace content

#endif  // CONTENT_BROWSER_BROWSER_PLUGIN_TEST_BROWSER_PLUGIN_GUEST_H_
