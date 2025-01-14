// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/session_state_animator.h"

#include "ash/shell.h"
#include "ash/shell_window_ids.h"
#include "ash/wm/window_animations.h"
#include "ash/wm/workspace/workspace_animations.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/root_window.h"
#include "ui/compositor/layer_animation_observer.h"
#include "ui/compositor/layer_animation_sequence.h"
#include "ui/views/widget/widget.h"

namespace ash {
namespace internal {

namespace {

// Slightly-smaller size that we scale the screen down to for the pre-lock and
// pre-shutdown states.
const float kSlowCloseSizeRatio = 0.95f;

// Maximum opacity of white layer when animating pre-shutdown state.
const float kPartialFadeRatio = 0.3f;

// Returns the transform that should be applied to containers for the slow-close
// animation.
gfx::Transform GetSlowCloseTransform() {
  gfx::Size root_size = Shell::GetPrimaryRootWindow()->bounds().size();
  gfx::Transform transform;
  transform.SetScale(kSlowCloseSizeRatio, kSlowCloseSizeRatio);
  transform.ConcatTranslate(
      floor(0.5 * (1.0 - kSlowCloseSizeRatio) * root_size.width() + 0.5),
      floor(0.5 * (1.0 - kSlowCloseSizeRatio) * root_size.height() + 0.5));
  return transform;
}

// Returns the transform that should be applied to containers for the fast-close
// animation.
gfx::Transform GetFastCloseTransform() {
  gfx::Size root_size = Shell::GetPrimaryRootWindow()->bounds().size();
  gfx::Transform transform;
  transform.SetScale(0.0, 0.0);
  transform.ConcatTranslate(floor(0.5 * root_size.width() + 0.5),
                            floor(0.5 * root_size.height() + 0.5));
  return transform;
}

// Slowly shrinks |window| to a slightly-smaller size.
void StartSlowCloseAnimationForWindow(aura::Window* window,
                                      base::TimeDelta duration,
                                      ui::LayerAnimationObserver* observer) {
  ui::LayerAnimator* animator = window->layer()->GetAnimator();
  animator->set_preemption_strategy(
      ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
  ui::LayerAnimationSequence* sequence = new ui::LayerAnimationSequence(
      ui::LayerAnimationElement::CreateTransformElement(
          GetSlowCloseTransform(),
          duration));
  animator->StartAnimation(sequence);
  if (observer)
    sequence->AddObserver(observer);
}

// Quickly undoes the effects of the slow-close animation on |window|.
void StartUndoSlowCloseAnimationForWindow(
    aura::Window* window,
    base::TimeDelta duration,
    ui::LayerAnimationObserver* observer) {
  ui::LayerAnimator* animator = window->layer()->GetAnimator();
  animator->set_preemption_strategy(
      ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
  ui::LayerAnimationSequence* sequence = new ui::LayerAnimationSequence(
      ui::LayerAnimationElement::CreateTransformElement(
          gfx::Transform(),
          duration));
  animator->StartAnimation(sequence);
  if (observer)
    sequence->AddObserver(observer);
}

// Quickly shrinks |window| down to a point in the center of the screen and
// fades it out to 0 opacity.
void StartFastCloseAnimationForWindow(aura::Window* window,
                                      base::TimeDelta duration,
                                      ui::LayerAnimationObserver* observer) {
  ui::LayerAnimator* animator = window->layer()->GetAnimator();
  animator->set_preemption_strategy(
      ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
  animator->StartAnimation(
      new ui::LayerAnimationSequence(
          ui::LayerAnimationElement::CreateTransformElement(
              GetFastCloseTransform(), duration)));
  ui::LayerAnimationSequence* sequence = new ui::LayerAnimationSequence(
      ui::LayerAnimationElement::CreateOpacityElement(0.0, duration));
  animator->StartAnimation(sequence);
  if (observer)
    sequence->AddObserver(observer);
}

// Fades |window| to |target_opacity| over |duration|.
void StartPartialFadeAnimation(aura::Window* window,
                               float target_opacity,
                               base::TimeDelta duration,
                               ui::LayerAnimationObserver* observer) {
  ui::LayerAnimator* animator = window->layer()->GetAnimator();
  animator->set_preemption_strategy(
      ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
  ui::LayerAnimationSequence* sequence = new ui::LayerAnimationSequence(
      ui::LayerAnimationElement::CreateOpacityElement(
          target_opacity, duration));
  animator->StartAnimation(sequence);
  if (observer)
    sequence->AddObserver(observer);
}

// Fades |window| in to full opacity over |duration|.
void FadeInWindow(aura::Window* window,
                  base::TimeDelta duration,
                  ui::LayerAnimationObserver* observer) {
  ui::LayerAnimator* animator = window->layer()->GetAnimator();
  animator->set_preemption_strategy(
      ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
  ui::LayerAnimationSequence* sequence = new ui::LayerAnimationSequence(
      ui::LayerAnimationElement::CreateOpacityElement(1.0, duration));
  animator->StartAnimation(sequence);
  if (observer)
    sequence->AddObserver(observer);
}

// Makes |window| fully transparent instantaneously.
void HideWindowImmediately(aura::Window* window,
                           ui::LayerAnimationObserver* observer) {
  window->layer()->SetOpacity(0.0);
  if (observer)
    observer->OnLayerAnimationEnded(NULL);
}

// Restores |window| to its original position and scale and full opacity
// instantaneously.
void RestoreWindow(aura::Window* window, ui::LayerAnimationObserver* observer) {
  window->layer()->SetTransform(gfx::Transform());
  window->layer()->SetOpacity(1.0);
  if (observer)
    observer->OnLayerAnimationEnded(NULL);
}

void HideWindow(aura::Window* window,
                base::TimeDelta duration,
                WorkspaceAnimationDirection direction,
                ui::LayerAnimationObserver* observer) {
  WorkspaceAnimationDetails details;
  details.direction = direction;
  details.animate = true;
  details.animate_scale = true;
  details.animate_opacity = true;
  details.duration = duration;
  HideWorkspace(window, details);
  // A bit of a dirty trick: we need to catch the end of the animation we don't
  // control. So we use two facts we know: which animator will be used and the
  // target opacity to add "Do nothing" animation sequence.
  // Unfortunately, we can not just use empty LayerAnimationSequence, because
  // it does not call NotifyEnded().
  ui::LayerAnimationSequence* sequence = new ui::LayerAnimationSequence(
      ui::LayerAnimationElement::CreateOpacityElement(
          0.0, base::TimeDelta()));
  if (observer)
    sequence->AddObserver(observer);
  window->layer()->GetAnimator()->ScheduleAnimation(sequence);
}

void ShowWindow(aura::Window* window,
                base::TimeDelta length,
                WorkspaceAnimationDirection direction,
                ui::LayerAnimationObserver* observer) {
  WorkspaceAnimationDetails details;
  details.direction = direction;
  details.animate = true;
  details.animate_scale = true;
  details.animate_opacity = true;
  details.duration = length;
  ShowWorkspace(window, details);
  // A bit of a dirty trick: we need to catch the end of the animation we don't
  // control. So we use two facts we know: which animator will be used and the
  // target opacity to add "Do nothing" animation sequence.
  // Unfortunately, we can not just use empty LayerAnimationSequence, because
  // it does not call NotifyEnded().
  ui::LayerAnimationSequence* sequence = new ui::LayerAnimationSequence(
      ui::LayerAnimationElement::CreateOpacityElement(
          1.0, base::TimeDelta()));
  if (observer)
    sequence->AddObserver(observer);
  window->layer()->GetAnimator()->ScheduleAnimation(sequence);
}

// Starts grayscale/brightness animation for |window| over |duration|. Target
// value for both grayscale and brightness are specified by |target|.
void StartGrayscaleBrightnessAnimationForWindow(
    aura::Window* window,
    float target,
    base::TimeDelta duration,
    ui::LayerAnimationObserver* observer) {
  ui::LayerAnimator* animator = window->layer()->GetAnimator();

  std::vector<ui::LayerAnimationSequence*> animations =
      CreateBrightnessGrayscaleAnimationSequence(target, duration);

  if (observer)
    animations[0]->AddObserver(observer);

  animator->set_preemption_strategy(
      ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);

  animator->StartTogether(animations);
}

// Animation observer that will drop animated foreground once animation is
// finished. It is used in when undoing shutdown animation.
class CallbackAnimationObserver : public ui::LayerAnimationObserver {
 public:
  explicit CallbackAnimationObserver(base::Callback<void(void)> &callback)
      : callback_(callback) {
  }
  virtual ~CallbackAnimationObserver() {
  }

 private:
  // Overridden from ui::LayerAnimationObserver:
  virtual void OnLayerAnimationEnded(ui::LayerAnimationSequence* seq)
      OVERRIDE {
    // Drop foreground once animation is over.
    callback_.Run();
    delete this;
  }

  virtual void OnLayerAnimationAborted(ui::LayerAnimationSequence* seq)
      OVERRIDE {
    // Drop foreground once animation is over.
    callback_.Run();
    delete this;
  }

  virtual void OnLayerAnimationScheduled(ui::LayerAnimationSequence* seq)
      OVERRIDE {}

  base::Callback<void(void)> callback_;

  DISALLOW_COPY_AND_ASSIGN(CallbackAnimationObserver);
};

}  // namespace

bool SessionStateAnimator::TestApi::ContainersAreAnimated(
    int container_mask, AnimationType type) const {
  aura::Window::Windows containers;
  animator_->GetContainers(container_mask, &containers);
  for (aura::Window::Windows::const_iterator it = containers.begin();
       it != containers.end(); ++it) {
    aura::Window* window = *it;
    ui::Layer* layer = window->layer();

    switch (type) {
      case ANIMATION_PARTIAL_CLOSE:
        if (layer->GetTargetTransform() != GetSlowCloseTransform())
          return false;
        break;
      case ANIMATION_UNDO_PARTIAL_CLOSE:
        if (layer->GetTargetTransform() != gfx::Transform())
          return false;
        break;
      case ANIMATION_FULL_CLOSE:
        if (layer->GetTargetTransform() != GetFastCloseTransform() ||
            layer->GetTargetOpacity() > 0.0001)
          return false;
        break;
      case ANIMATION_FADE_IN:
        if (layer->GetTargetOpacity() < 0.9999)
          return false;
        break;
      case ANIMATION_HIDE_IMMEDIATELY:
        if (layer->GetTargetOpacity() > 0.0001)
          return false;
        break;
      case ANIMATION_RESTORE:
        if (layer->opacity() < 0.9999 || layer->transform() != gfx::Transform())
          return false;
        break;
      default:
        NOTREACHED() << "Unhandled animation type " << type;
        return false;
    }
  }
  return true;
}

const int SessionStateAnimator::kAllLockScreenContainersMask =
    SessionStateAnimator::LOCK_SCREEN_BACKGROUND |
    SessionStateAnimator::LOCK_SCREEN_CONTAINERS |
    SessionStateAnimator::LOCK_SCREEN_RELATED_CONTAINERS;

const int SessionStateAnimator::kAllContainersMask =
    SessionStateAnimator::kAllLockScreenContainersMask |
    SessionStateAnimator::DESKTOP_BACKGROUND |
    SessionStateAnimator::LAUNCHER |
    SessionStateAnimator::NON_LOCK_SCREEN_CONTAINERS;

SessionStateAnimator::SessionStateAnimator() {
}

SessionStateAnimator::~SessionStateAnimator() {
}

base::TimeDelta SessionStateAnimator::GetDuration(AnimationSpeed speed) {
  switch (speed) {
    case ANIMATION_SPEED_IMMEDIATE:
      return base::TimeDelta();
    case ANIMATION_SPEED_UNDOABLE:
      return base::TimeDelta::FromMilliseconds(400);
    case ANIMATION_SPEED_REVERT:
      return base::TimeDelta::FromMilliseconds(100);
    case ANIMATION_SPEED_FAST:
      return base::TimeDelta::FromMilliseconds(150);
    case ANIMATION_SPEED_SHOW_LOCK_SCREEN:
      return base::TimeDelta::FromMilliseconds(200);
    case ANIMATION_SPEED_MOVE_WINDOWS:
      return base::TimeDelta::FromMilliseconds(400);
    case ANIMATION_SPEED_SHUTDOWN:
      return base::TimeDelta::FromMilliseconds(1000);
    case ANIMATION_SPEED_REVERT_SHUTDOWN:
      return base::TimeDelta::FromMilliseconds(400);
  }
  // Satisfy compilers that do not understand that we will return from switch
  // above anyway.
  DCHECK(false) << "Unhandled animation speed " << speed;
  return base::TimeDelta();
}

// Fills |containers| with the containers described by |container_mask|.
void SessionStateAnimator::GetContainers(int container_mask,
                                         aura::Window::Windows* containers) {
  aura::RootWindow* root_window = Shell::GetPrimaryRootWindow();
  containers->clear();

  if (container_mask & DESKTOP_BACKGROUND) {
    containers->push_back(Shell::GetContainer(
        root_window,
        internal::kShellWindowId_DesktopBackgroundContainer));
  }
  if (container_mask & LAUNCHER) {
    containers->push_back(Shell::GetContainer(
        root_window,
        internal::kShellWindowId_LauncherContainer));
  }
  if (container_mask & NON_LOCK_SCREEN_CONTAINERS) {
    // TODO(antrim): Figure out a way to eliminate a need to exclude launcher
    // in such way.
    aura::Window* non_lock_screen_containers = Shell::GetContainer(
        root_window,
        internal::kShellWindowId_NonLockScreenContainersContainer);
    aura::Window::Windows children = non_lock_screen_containers->children();

    for (aura::Window::Windows::const_iterator it = children.begin();
         it != children.end(); ++it) {
      aura::Window* window = *it;
      if (window->id() == internal::kShellWindowId_LauncherContainer)
        continue;
      containers->push_back(window);
    }
  }
  if (container_mask & LOCK_SCREEN_BACKGROUND) {
    containers->push_back(Shell::GetContainer(
        root_window,
        internal::kShellWindowId_LockScreenBackgroundContainer));
  }
  if (container_mask & LOCK_SCREEN_CONTAINERS) {
    containers->push_back(Shell::GetContainer(
        root_window,
        internal::kShellWindowId_LockScreenContainersContainer));
  }
  if (container_mask & LOCK_SCREEN_RELATED_CONTAINERS) {
    containers->push_back(Shell::GetContainer(
        root_window,
        internal::kShellWindowId_LockScreenRelatedContainersContainer));
  }
  if (container_mask & LOCK_SCREEN_SYSTEM_FOREGROUND) {
    containers->push_back(Shell::GetContainer(
        root_window,
        internal::kShellWindowId_PowerButtonAnimationContainer));
  }
}

void SessionStateAnimator::StartAnimation(int container_mask,
                                          AnimationType type,
                                          AnimationSpeed speed) {
  aura::Window::Windows containers;
  GetContainers(container_mask, &containers);
  for (aura::Window::Windows::const_iterator it = containers.begin();
       it != containers.end(); ++it) {
    RunAnimationForWindow(*it, type, speed, NULL);
  }
}

// Apply animation |type| to all containers described by |container_mask|.
void SessionStateAnimator::StartAnimationWithCallback(
    int container_mask,
    AnimationType type,
    AnimationSpeed speed,
    base::Callback<void(void)>& callback) {
  aura::Window::Windows containers;
  GetContainers(container_mask, &containers);
  for (aura::Window::Windows::const_iterator it = containers.begin();
       it != containers.end(); ++it) {
    ui::LayerAnimationObserver* observer =
        new CallbackAnimationObserver(callback);
    RunAnimationForWindow(*it, type, speed, observer);
  }
}

void SessionStateAnimator::StartGlobalAnimation(AnimationType type,
                                                AnimationSpeed speed) {
  aura::RootWindow* root_window = Shell::GetPrimaryRootWindow();
  RunAnimationForWindow(root_window, type, speed, NULL);
}

void SessionStateAnimator::RunAnimationForWindow(
    aura::Window* window,
    AnimationType type,
    AnimationSpeed speed,
    ui::LayerAnimationObserver* observer) {
  base::TimeDelta duration = GetDuration(speed);

  switch (type) {
    case ANIMATION_PARTIAL_CLOSE:
      StartSlowCloseAnimationForWindow(window, duration, observer);
      break;
    case ANIMATION_UNDO_PARTIAL_CLOSE:
      StartUndoSlowCloseAnimationForWindow(window, duration, observer);
      break;
    case ANIMATION_FULL_CLOSE:
      StartFastCloseAnimationForWindow(window, duration, observer);
      break;
    case ANIMATION_FADE_IN:
      FadeInWindow(window, duration, observer);
      break;
    case ANIMATION_HIDE_IMMEDIATELY:
      DCHECK_EQ(speed, ANIMATION_SPEED_IMMEDIATE);
      HideWindowImmediately(window, observer);
      break;
    case ANIMATION_RESTORE:
      DCHECK_EQ(speed, ANIMATION_SPEED_IMMEDIATE);
      RestoreWindow(window, observer);
      break;
    case ANIMATION_LIFT:
      HideWindow(window, duration, WORKSPACE_ANIMATE_UP, observer);
      break;
    case ANIMATION_DROP:
      ShowWindow(window, duration, WORKSPACE_ANIMATE_DOWN, observer);
      break;
    case ANIMATION_RAISE_TO_SCREEN:
      ShowWindow(window, duration, WORKSPACE_ANIMATE_UP, observer);
      break;
    case ANIMATION_LOWER_BELOW_SCREEN:
      HideWindow(window, duration, WORKSPACE_ANIMATE_DOWN, observer);
      break;
    case ANIMATION_PARTIAL_FADE_IN:
      StartPartialFadeAnimation(
          window, kPartialFadeRatio, duration, observer);
      break;
    case ANIMATION_UNDO_PARTIAL_FADE_IN:
      StartPartialFadeAnimation(window, 0.0, duration, observer);
      break;
    case ANIMATION_FULL_FADE_IN:
      StartPartialFadeAnimation(window, 1.0, duration, observer);
      break;
    case ANIMATION_GRAYSCALE_BRIGHTNESS:
      StartGrayscaleBrightnessAnimationForWindow(
          window, 1.0, duration, observer);
      break;
    case ANIMATION_UNDO_GRAYSCALE_BRIGHTNESS:
      StartGrayscaleBrightnessAnimationForWindow(
          window, 0.0, duration, observer);
      break;
  }
}

void SessionStateAnimator::CreateForeground() {
  if (foreground_.get())
    return;
  aura::Window* window = Shell::GetContainer(
      Shell::GetPrimaryRootWindow(),
      internal::kShellWindowId_PowerButtonAnimationContainer);
  HideWindowImmediately(window, NULL);
  foreground_.reset(
      new ColoredWindowController(window, "SessionStateAnimatorForeground"));
  foreground_->SetColor(SK_ColorWHITE);
  foreground_->GetWidget()->Show();
}

void SessionStateAnimator::DropForeground() {
  foreground_.reset();
}

}  // namespace internal
}  // namespace ash
