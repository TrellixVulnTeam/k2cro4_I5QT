// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/login/login_prompt.h"

#include "base/string16.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/password_manager/password_manager.h"
#include "chrome/browser/tab_contents/tab_util.h"
#include "chrome/browser/ui/tab_contents/tab_contents.h"
#include "chrome/browser/ui/views/constrained_window_views.h"
#include "chrome/browser/ui/views/login_view.h"
#include "chrome/common/chrome_switches.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "grit/generated_resources.h"
#include "net/url_request/url_request.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/views/window/dialog_delegate.h"

using content::BrowserThread;
using content::PasswordForm;
using content::WebContents;

// ----------------------------------------------------------------------------
// LoginHandlerViews

// This class simply forwards the authentication from the LoginView (on
// the UI thread) to the net::URLRequest (on the I/O thread).
// This class uses ref counting to ensure that it lives until all InvokeLaters
// have been called.
class LoginHandlerViews : public LoginHandler,
                          public views::DialogDelegate {
 public:
  LoginHandlerViews(net::AuthChallengeInfo* auth_info, net::URLRequest* request)
      : LoginHandler(auth_info, request),
        enable_chrome_style_(chrome::UseChromeStyleDialogs()),
        login_view_(NULL) {
  }

  // LoginModelObserver implementation.
  virtual void OnAutofillDataAvailable(const string16& username,
                                       const string16& password) OVERRIDE {
    // Nothing to do here since LoginView takes care of autofill for win.
  }

  // views::DialogDelegate methods:
  virtual string16 GetDialogButtonLabel(
      ui::DialogButton button) const OVERRIDE {
    if (button == ui::DIALOG_BUTTON_OK)
      return l10n_util::GetStringUTF16(IDS_LOGIN_DIALOG_OK_BUTTON_LABEL);
    return DialogDelegate::GetDialogButtonLabel(button);
  }

  virtual string16 GetWindowTitle() const OVERRIDE {
    return l10n_util::GetStringUTF16(IDS_LOGIN_DIALOG_TITLE);
  }

  virtual void WindowClosing() OVERRIDE {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

    WebContents* tab = GetWebContentsForLogin();
    if (tab)
      tab->GetRenderViewHost()->SetIgnoreInputEvents(false);

    // Reference is no longer valid.
    SetDialog(NULL);

    CancelAuth();
  }

  virtual void DeleteDelegate() OVERRIDE {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

    // The constrained window is going to delete itself; clear our pointer.
    SetDialog(NULL);
    SetModel(NULL);

    ReleaseSoon();
  }

  virtual bool UseChromeStyle() const OVERRIDE {
    return enable_chrome_style_;
  }

  virtual bool Cancel() OVERRIDE {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

    CancelAuth();
    return true;
  }

  virtual bool Accept() OVERRIDE {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

    SetAuth(login_view_->GetUsername(), login_view_->GetPassword());
    return true;
  }

  virtual views::View* GetInitiallyFocusedView() OVERRIDE {
    return login_view_->GetInitiallyFocusedView();
  }

  virtual views::View* GetContentsView() OVERRIDE {
    return login_view_;
  }
  virtual views::Widget* GetWidget() OVERRIDE {
    return login_view_->GetWidget();
  }
  virtual const views::Widget* GetWidget() const OVERRIDE {
    return login_view_->GetWidget();
  }

  // LoginHandler:

  virtual void BuildViewForPasswordManager(
      PasswordManager* manager,
      const string16& explanation) OVERRIDE {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

    // Create a new LoginView and set the model for it.  The model
    // (password manager) is owned by the view's parent WebContents,
    // so natural destruction order means we don't have to worry about
    // disassociating the model from the view, because the view will
    // be deleted before the password manager.
    login_view_ = new LoginView(explanation, manager, enable_chrome_style_);

    // Scary thread safety note: This can potentially be called *after* SetAuth
    // or CancelAuth (say, if the request was cancelled before the UI thread got
    // control).  However, that's OK since any UI interaction in those functions
    // will occur via an InvokeLater on the UI thread, which is guaranteed
    // to happen after this is called (since this was InvokeLater'd first).
    WebContents* requesting_contents = GetWebContentsForLogin();
    SetDialog(new ConstrainedWindowViews(
        requesting_contents,
        this,
        enable_chrome_style_,
        ConstrainedWindowViews::DEFAULT_INSETS));
    NotifyAuthNeeded();
  }

 private:
  friend class base::RefCountedThreadSafe<LoginHandlerViews>;
  friend class LoginPrompt;

  ~LoginHandlerViews() {}

  bool enable_chrome_style_;

  // The LoginView that contains the user's login information
  LoginView* login_view_;

  DISALLOW_COPY_AND_ASSIGN(LoginHandlerViews);
};

// static
LoginHandler* LoginHandler::Create(net::AuthChallengeInfo* auth_info,
                                   net::URLRequest* request) {
  return new LoginHandlerViews(auth_info, request);
}
