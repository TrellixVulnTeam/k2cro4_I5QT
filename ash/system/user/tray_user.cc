// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/user/tray_user.h"

#include "ash/shell.h"
#include "ash/system/tray/system_tray_delegate.h"
#include "ash/system/tray/tray_constants.h"
#include "ash/system/tray/tray_item_view.h"
#include "ash/system/tray/tray_views.h"
#include "base/utf_string_conversions.h"
#include "grit/ash_strings.h"
#include "skia/ext/image_operations.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "third_party/skia/include/core/SkPath.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/gfx/size.h"
#include "ui/gfx/skia_util.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/text_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace {

const int kUserInfoVerticalPadding = 10;
const int kUserIconSize = 27;
const int kProfileRoundedCornerRadius = 2;

}  // namespace

namespace ash {
namespace internal {

namespace tray {

// A custom image view with rounded edges.
class RoundedImageView : public views::View {
 public:
  // Constructs a new rounded image view with rounded corners of radius
  // |corner_radius|.
  explicit RoundedImageView(int corner_radius) : corner_radius_(corner_radius) {
  }

  virtual ~RoundedImageView() {
  }

  // Set the image that should be displayed from a pointer. The pointer
  // contents is copied in the receiver's image.
  void SetImage(const gfx::ImageSkia& img, const gfx::Size& size) {
    image_ = img;
    image_size_ = size;

    // Try to get the best image quality for the avatar.
    resized_ = gfx::ImageSkiaOperations::CreateResizedImage(image_,
        skia::ImageOperations::RESIZE_BEST, size);
    if (GetWidget() && visible()) {
      PreferredSizeChanged();
      SchedulePaint();
    }
  }

  // Overridden from views::View.
  virtual gfx::Size GetPreferredSize() OVERRIDE {
    return gfx::Size(image_size_.width() + GetInsets().width(),
                     image_size_.height() + GetInsets().height());
  }

  virtual void OnPaint(gfx::Canvas* canvas) OVERRIDE {
    View::OnPaint(canvas);
    gfx::Rect image_bounds(size());
    image_bounds.ClampToCenteredSize(GetPreferredSize());
    image_bounds.Inset(GetInsets());
    const SkScalar kRadius = SkIntToScalar(corner_radius_);
    SkPath path;
    path.addRoundRect(gfx::RectToSkRect(image_bounds), kRadius, kRadius);
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setXfermodeMode(SkXfermode::kSrcOver_Mode);
    canvas->DrawImageInPath(resized_, image_bounds.x(), image_bounds.y(),
                            path, paint);
  }

 private:
  gfx::ImageSkia image_;
  gfx::ImageSkia resized_;
  gfx::Size image_size_;
  int corner_radius_;

  DISALLOW_COPY_AND_ASSIGN(RoundedImageView);
};

class UserView : public views::View,
                 public views::ButtonListener {
 public:
  explicit UserView(ash::user::LoginStatus login)
      : login_(login),
        user_info_(NULL),
        username_(NULL),
        email_(NULL),
        signout_(NULL) {
    CHECK(login_ != ash::user::LOGGED_IN_NONE);

    bool public_account = login_ == ash::user::LOGGED_IN_PUBLIC;
    bool guest = login_ == ash::user::LOGGED_IN_GUEST;
    bool locked = login_ == ash::user::LOGGED_IN_LOCKED;

    set_background(views::Background::CreateSolidBackground(
        public_account ? kPublicAccountBackgroundColor : kBackgroundColor));

    if (!guest)
      AddUserInfo();

    // A user should not be able to modify logged in state when screen is
    // locked.
    if (!locked)
      AddButtonContainer();
  }

  virtual ~UserView() {}

  // Create container for buttons.
  void AddButtonContainer() {
    TrayPopupLabelButton* button = new TrayPopupLabelButton(this,
        ash::user::GetLocalizedSignOutStringForStatus(login_, true));
    AddChildView(button);
    signout_ = button;
  }

 private:
  void AddUserInfo() {
    user_info_ = new views::View;
    user_info_->SetLayoutManager(new views::BoxLayout(
        views::BoxLayout::kHorizontal, kTrayPopupPaddingHorizontal,
        kUserInfoVerticalPadding, kTrayPopupPaddingBetweenItems));
    AddChildView(user_info_);

    if (login_ == ash::user::LOGGED_IN_KIOSK) {
      views::Label* label = new views::Label;
      ui::ResourceBundle& bundle = ui::ResourceBundle::GetSharedInstance();
      label->SetText(
          bundle.GetLocalizedString(IDS_ASH_STATUS_TRAY_KIOSK_LABEL));
      label->set_border(views::Border::CreateEmptyBorder(
            0, 4, 0, 1));
      label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      user_info_->AddChildView(label);
      return;
    }

    RoundedImageView* image = new RoundedImageView(kProfileRoundedCornerRadius);
    image->SetImage(ash::Shell::GetInstance()->tray_delegate()->GetUserImage(),
        gfx::Size(kUserIconSize, kUserIconSize));
    user_info_->AddChildView(image);

    views::View* user = new views::View;
    user->SetLayoutManager(new views::BoxLayout(
        views::BoxLayout::kVertical, 0, 5, 0));
    ash::SystemTrayDelegate* tray =
        ash::Shell::GetInstance()->tray_delegate();
    username_ = new views::Label(tray->GetUserDisplayName());
    username_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    user->AddChildView(username_);

    email_ = new views::Label(UTF8ToUTF16(tray->GetUserEmail()));
    email_->SetFont(username_->font().DeriveFont(-1));
    email_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    email_->SetEnabled(false);
    user->AddChildView(email_);

    user_info_->AddChildView(user);
  }

  // Overridden from views::ButtonListener.
  virtual void ButtonPressed(views::Button* sender,
                             const ui::Event& event) OVERRIDE {
    CHECK(sender == signout_);
    ash::SystemTrayDelegate* tray = ash::Shell::GetInstance()->tray_delegate();
    tray->SignOut();
  }

  // Overridden from views::View.
  virtual gfx::Size GetPreferredSize() OVERRIDE {
    gfx::Size size;
    if (user_info_)
      size = user_info_->GetPreferredSize();
    if (signout_) {
      gfx::Size signout_size = signout_->GetPreferredSize();
      // Make sure the user default view item at least as tall as the other
      // tray popup items.
      if (size.height() == 0)
        size.set_height(kTrayPopupItemHeight);
      size.set_height(std::max(size.height(), signout_size.height()));
      size.set_width(size.width() + signout_size.width() +
          kTrayPopupPaddingHorizontal * 2 + kTrayPopupPaddingBetweenItems);
    }
    return size;
  }

  virtual void Layout() OVERRIDE {
    views::View::Layout();
    if (bounds().IsEmpty())
      return;

    if (signout_ && user_info_) {
      gfx::Rect signout_bounds(bounds());
      signout_bounds.ClampToCenteredSize(signout_->GetPreferredSize());
      signout_bounds.set_x(width() - signout_bounds.width() -
          kTrayPopupPaddingHorizontal);
      signout_->SetBoundsRect(signout_bounds);

      gfx::Rect usercard_bounds(user_info_->GetPreferredSize());
      usercard_bounds.set_width(signout_bounds.x());
      user_info_->SetBoundsRect(usercard_bounds);
    } else if (signout_) {
      signout_->SetBoundsRect(gfx::Rect(size()));
    } else if (user_info_) {
      user_info_->SetBoundsRect(gfx::Rect(size()));
    }
  }

  user::LoginStatus login_;

  views::View* user_info_;
  views::Label* username_;
  views::Label* email_;

  views::Button* signout_;

  DISALLOW_COPY_AND_ASSIGN(UserView);
};

}  // namespace tray

TrayUser::TrayUser(SystemTray* system_tray)
    : SystemTrayItem(system_tray),
      user_(NULL),
      avatar_(NULL),
      label_(NULL) {
}

TrayUser::~TrayUser() {
}

views::View* TrayUser::CreateTrayView(user::LoginStatus status) {
  CHECK(avatar_ == NULL);
  CHECK(label_ == NULL);
  if (status == user::LOGGED_IN_GUEST) {
    label_ = new views::Label;
    ui::ResourceBundle& bundle = ui::ResourceBundle::GetSharedInstance();
    label_->SetText(bundle.GetLocalizedString(IDS_ASH_STATUS_TRAY_GUEST_LABEL));
    SetupLabelForTray(label_);
  } else {
    avatar_ = new tray::RoundedImageView(kProfileRoundedCornerRadius);
  }
  UpdateAfterLoginStatusChange(status);
  return avatar_ ? static_cast<views::View*>(avatar_)
                 : static_cast<views::View*>(label_);
}

views::View* TrayUser::CreateDefaultView(user::LoginStatus status) {
  if (status == user::LOGGED_IN_NONE)
    return NULL;

  CHECK(user_ == NULL);
  user_ = new tray::UserView(status);
  return user_;
}

views::View* TrayUser::CreateDetailedView(user::LoginStatus status) {
  return NULL;
}

void TrayUser::DestroyTrayView() {
  avatar_ = NULL;
  label_ = NULL;
}

void TrayUser::DestroyDefaultView() {
  user_ = NULL;
}

void TrayUser::DestroyDetailedView() {
}

void TrayUser::UpdateAfterLoginStatusChange(user::LoginStatus status) {
  switch (status) {
    case user::LOGGED_IN_LOCKED:
    case user::LOGGED_IN_USER:
    case user::LOGGED_IN_OWNER:
    case user::LOGGED_IN_PUBLIC:
      avatar_->SetImage(
          ash::Shell::GetInstance()->tray_delegate()->GetUserImage(),
          gfx::Size(kUserIconSize, kUserIconSize));
      avatar_->SetVisible(true);
      break;

    case user::LOGGED_IN_GUEST:
      label_->SetVisible(true);
      break;

    case user::LOGGED_IN_KIOSK:
    case user::LOGGED_IN_NONE:
      avatar_->SetVisible(false);
      break;
  }
}

void TrayUser::UpdateAfterShelfAlignmentChange(ShelfAlignment alignment) {
  if (avatar_) {
    if (alignment == SHELF_ALIGNMENT_BOTTOM) {
      avatar_->set_border(views::Border::CreateEmptyBorder(
          0, kTrayImageItemHorizontalPaddingBottomAlignment + 2,
          0, kTrayImageItemHorizontalPaddingBottomAlignment));
    } else {
        SetTrayImageItemBorder(avatar_, alignment);
    }
  } else {
    if (alignment == SHELF_ALIGNMENT_BOTTOM) {
      label_->set_border(views::Border::CreateEmptyBorder(
          0, kTrayLabelItemHorizontalPaddingBottomAlignment,
          0, kTrayLabelItemHorizontalPaddingBottomAlignment));
    } else {
      label_->set_border(views::Border::CreateEmptyBorder(
          kTrayLabelItemVerticalPaddingVeriticalAlignment,
          kTrayLabelItemHorizontalPaddingBottomAlignment,
          kTrayLabelItemVerticalPaddingVeriticalAlignment,
          kTrayLabelItemHorizontalPaddingBottomAlignment));
    }
  }
}

void TrayUser::OnUserUpdate() {
  // Check for null to avoid crbug.com/150944.
  if (avatar_) {
    avatar_->SetImage(
        ash::Shell::GetInstance()->tray_delegate()->GetUserImage(),
        gfx::Size(kUserIconSize, kUserIconSize));
  }
}

}  // namespace internal
}  // namespace ash
