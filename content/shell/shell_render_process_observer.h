// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SHELL_SHELL_RENDER_PROCESS_OBSERVER_H_
#define CONTENT_SHELL_SHELL_RENDER_PROCESS_OBSERVER_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "content/public/renderer/render_process_observer.h"

namespace WebKit {
class WebFrame;
}

namespace WebTestRunner {
class WebTestDelegate;
class WebTestInterfaces;
}

namespace content {

class RenderView;

class ShellRenderProcessObserver : public RenderProcessObserver {
 public:
  static ShellRenderProcessObserver* GetInstance();

  ShellRenderProcessObserver();
  virtual ~ShellRenderProcessObserver();

  void SetMainWindow(RenderView* view,
                     WebTestRunner::WebTestDelegate* delegate);
  void BindTestRunnersToWindow(WebKit::WebFrame* frame);

  // RenderProcessObserver implementation.
  virtual void WebKitInitialized() OVERRIDE;
  virtual bool OnControlMessageReceived(const IPC::Message& message) OVERRIDE;

  WebTestRunner::WebTestInterfaces* test_interfaces() const {
    return test_interfaces_.get();
  }

 private:
  // Message handlers.
  void OnResetAll();

  scoped_ptr<WebTestRunner::WebTestInterfaces> test_interfaces_;
  WebTestRunner::WebTestDelegate* test_delegate_;

  DISALLOW_COPY_AND_ASSIGN(ShellRenderProcessObserver);
};

}  // namespace content

#endif  // CONTENT_SHELL_SHELL_RENDER_PROCESS_OBSERVER_H_
