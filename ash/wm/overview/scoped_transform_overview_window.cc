// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/overview/scoped_transform_overview_window.h"

#include "ash/screen_ash.h"
#include "ash/shell.h"
#include "ash/wm/window_state.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/client/screen_position_client.h"
#include "ui/aura/root_window.h"
#include "ui/aura/window.h"
#include "ui/compositor/layer_animation_observer.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/display.h"
#include "ui/gfx/interpolated_transform.h"
#include "ui/gfx/transform_util.h"
#include "ui/views/corewm/shadow_types.h"
#include "ui/views/corewm/window_animations.h"
#include "ui/views/corewm/window_util.h"
#include "ui/views/widget/widget.h"

namespace ash {

namespace {

// Creates a copy of |window| with |recreated_layer| in the |target_root|.
views::Widget* CreateCopyOfWindow(aura::RootWindow* target_root,
                                  aura::Window* src_window,
                                  ui::Layer* recreated_layer) {
  // Save and remove the transform from the layer to later reapply to both the
  // source and newly created copy window.
  gfx::Transform transform = recreated_layer->transform();
  recreated_layer->SetTransform(gfx::Transform());

  src_window->SetTransform(transform);
  views::Widget* widget = new views::Widget;
  views::Widget::InitParams params(views::Widget::InitParams::TYPE_POPUP);
  params.opacity = views::Widget::InitParams::TRANSLUCENT_WINDOW;
  params.parent = src_window->parent();
  params.can_activate = false;
  params.keep_on_top = true;
  widget->set_focus_on_creation(false);
  widget->Init(params);
  widget->SetVisibilityChangedAnimationsEnabled(false);
  std::string name = src_window->name() + " (Copy)";
  widget->GetNativeWindow()->SetName(name);
  views::corewm::SetShadowType(widget->GetNativeWindow(),
                               views::corewm::SHADOW_TYPE_RECTANGULAR);

  // Set the bounds in the target root window.
  gfx::Display target_display =
      Shell::GetScreen()->GetDisplayNearestWindow(target_root);
  aura::client::ScreenPositionClient* screen_position_client =
      aura::client::GetScreenPositionClient(src_window->GetRootWindow());
  if (screen_position_client && target_display.is_valid()) {
    screen_position_client->SetBounds(widget->GetNativeWindow(),
        src_window->GetBoundsInScreen(), target_display);
  } else {
    widget->SetBounds(src_window->GetBoundsInScreen());
  }
  widget->StackAbove(src_window);

  // Move the |recreated_layer| to the newly created window.
  recreated_layer->set_delegate(src_window->layer()->delegate());
  gfx::Rect layer_bounds = recreated_layer->bounds();
  layer_bounds.set_origin(gfx::Point(0, 0));
  recreated_layer->SetBounds(layer_bounds);
  recreated_layer->SetVisible(false);
  recreated_layer->parent()->Remove(recreated_layer);

  aura::Window* window = widget->GetNativeWindow();
  recreated_layer->SetVisible(true);
  window->layer()->Add(recreated_layer);
  window->layer()->StackAtTop(recreated_layer);
  window->layer()->SetOpacity(1);
  window->SetTransform(transform);
  window->Show();
  return widget;
}

// An observer which closes the widget and deletes the layer after an
// animation finishes.
class CleanupWidgetAfterAnimationObserver : public ui::LayerAnimationObserver {
 public:
  CleanupWidgetAfterAnimationObserver(views::Widget* widget, ui::Layer* layer);

  // ui::LayerAnimationObserver:
  virtual void OnLayerAnimationEnded(
      ui::LayerAnimationSequence* sequence) OVERRIDE;
  virtual void OnLayerAnimationAborted(
      ui::LayerAnimationSequence* sequence) OVERRIDE;
  virtual void OnLayerAnimationScheduled(
      ui::LayerAnimationSequence* sequence) OVERRIDE;

 private:
  virtual ~CleanupWidgetAfterAnimationObserver();

  views::Widget* widget_;
  ui::Layer* layer_;

  DISALLOW_COPY_AND_ASSIGN(CleanupWidgetAfterAnimationObserver);
};

CleanupWidgetAfterAnimationObserver::CleanupWidgetAfterAnimationObserver(
        views::Widget* widget,
        ui::Layer* layer)
    : widget_(widget),
      layer_(layer) {
  widget_->GetNativeWindow()->layer()->GetAnimator()->AddObserver(this);
}

void CleanupWidgetAfterAnimationObserver::OnLayerAnimationEnded(
    ui::LayerAnimationSequence* sequence) {
  delete this;
}

void CleanupWidgetAfterAnimationObserver::OnLayerAnimationAborted(
    ui::LayerAnimationSequence* sequence) {
  delete this;
}

void CleanupWidgetAfterAnimationObserver::OnLayerAnimationScheduled(
    ui::LayerAnimationSequence* sequence) {
}

CleanupWidgetAfterAnimationObserver::~CleanupWidgetAfterAnimationObserver() {
  widget_->GetNativeWindow()->layer()->GetAnimator()->RemoveObserver(this);
  widget_->Close();
  widget_ = NULL;
  if (layer_) {
    views::corewm::DeepDeleteLayers(layer_);
    layer_ = NULL;
  }
}

// The animation settings used for window selector animations.
class WindowSelectorAnimationSettings
    : public ui::ScopedLayerAnimationSettings {
 public:
  WindowSelectorAnimationSettings(aura::Window* window) :
      ui::ScopedLayerAnimationSettings(window->layer()->GetAnimator()) {
    SetPreemptionStrategy(
        ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
    SetTransitionDuration(base::TimeDelta::FromMilliseconds(
        ScopedTransformOverviewWindow::kTransitionMilliseconds));
  }

  virtual ~WindowSelectorAnimationSettings() {
  }
};

void SetTransformOnWindow(aura::Window* window,
                          const gfx::Transform& transform,
                          bool animate) {
  if (animate) {
    WindowSelectorAnimationSettings animation_settings(window);
    window->SetTransform(transform);
  } else {
    window->SetTransform(transform);
  }
}

gfx::Transform TranslateTransformOrigin(const gfx::Vector2d& new_origin,
                                        const gfx::Transform& transform) {
  gfx::Transform result;
  result.Translate(-new_origin.x(), -new_origin.y());
  result.PreconcatTransform(transform);
  result.Translate(new_origin.x(), new_origin.y());
  return result;
}

void SetTransformOnWindowAndAllTransientChildren(
    aura::Window* window,
    const gfx::Transform& transform,
    bool animate) {
  SetTransformOnWindow(window, transform, animate);

  aura::Window::Windows transient_children = window->transient_children();
  for (aura::Window::Windows::iterator iter = transient_children.begin();
       iter != transient_children.end(); ++iter) {
    aura::Window* transient_child = *iter;
    gfx::Rect window_bounds = window->bounds();
    gfx::Rect child_bounds = transient_child->bounds();
    gfx::Transform transient_window_transform(
        TranslateTransformOrigin(child_bounds.origin() - window_bounds.origin(),
                                 transform));
    SetTransformOnWindow(transient_child, transient_window_transform, animate);
  }
}

aura::Window* GetModalTransientParent(aura::Window* window) {
  if (window->GetProperty(aura::client::kModalKey) == ui::MODAL_TYPE_WINDOW)
    return window->transient_parent();
  return NULL;
}

}  // namespace

const int ScopedTransformOverviewWindow::kTransitionMilliseconds = 100;

ScopedTransformOverviewWindow::ScopedTransformOverviewWindow(
        aura::Window* window)
    : window_(window),
      window_copy_(NULL),
      layer_(NULL),
      minimized_(window->GetProperty(aura::client::kShowStateKey) ==
                 ui::SHOW_STATE_MINIMIZED),
      ignored_by_shelf_(ash::wm::GetWindowState(window)->ignored_by_shelf()),
      overview_started_(false),
      original_transform_(window->layer()->GetTargetTransform()) {
}

ScopedTransformOverviewWindow::~ScopedTransformOverviewWindow() {
  if (window_) {
    WindowSelectorAnimationSettings animation_settings(window_);
    gfx::Transform transform;
    // If the initial window wasn't destroyed and we have copied the window
    // layer, the copy needs to be animated out.
    // CleanupWidgetAfterAnimationObserver will destroy the widget and
    // layer after the animation is complete.
    if (window_copy_)
      new CleanupWidgetAfterAnimationObserver(window_copy_, layer_);
    SetTransformOnWindowAndTransientChildren(original_transform_, true);
    window_copy_ = NULL;
    layer_ = NULL;
    if (minimized_ && window_->GetProperty(aura::client::kShowStateKey) !=
        ui::SHOW_STATE_MINIMIZED) {
      // Setting opacity 0 and visible false ensures that the property change
      // to SHOW_STATE_MINIMIZED will not animate the window from its original
      // bounds to the minimized position.
      // Hiding the window needs to be done before the target opacity is 0,
      // otherwise the layer's visibility will not be updated
      // (See VisibilityController::UpdateLayerVisibility).
      window_->Hide();
      window_->layer()->SetOpacity(0);
      window_->SetProperty(aura::client::kShowStateKey,
                           ui::SHOW_STATE_MINIMIZED);
    }
    ash::wm::GetWindowState(window_)->set_ignored_by_shelf(ignored_by_shelf_);
  } else if (window_copy_) {
    // If this class still owns a copy of the window, clean up the copy. This
    // will be the case if the window was destroyed.
    window_copy_->Close();
    if (layer_)
      views::corewm::DeepDeleteLayers(layer_);
    window_copy_ = NULL;
    layer_ = NULL;
  }
}

bool ScopedTransformOverviewWindow::Contains(const aura::Window* target) const {
  if (window_copy_ && window_copy_->GetNativeWindow()->Contains(target))
    return true;
  aura::Window* window = window_;
  while (window) {
    if (window->Contains(target))
      return true;
    window = GetModalTransientParent(window);
  }
  return false;
}

gfx::Rect ScopedTransformOverviewWindow::GetBoundsInScreen() const {
  gfx::Rect bounds;
  aura::Window* window = window_;
  while (window) {
    bounds.Union(ScreenAsh::ConvertRectToScreen(window->parent(),
                                                window->GetTargetBounds()));
    window = GetModalTransientParent(window);
  }
  return bounds;
}

void ScopedTransformOverviewWindow::RestoreWindow() {
  if (minimized_ && window_->GetProperty(aura::client::kShowStateKey) ==
      ui::SHOW_STATE_MINIMIZED) {
    window_->Show();
  }
}

void ScopedTransformOverviewWindow::RestoreWindowOnExit() {
  minimized_ = false;
  original_transform_ = gfx::Transform();
}

void ScopedTransformOverviewWindow::OnWindowDestroyed() {
  window_ = NULL;
}

gfx::Rect ScopedTransformOverviewWindow::ShrinkRectToFitPreservingAspectRatio(
    const gfx::Rect& rect,
    const gfx::Rect& bounds) {
  DCHECK(!rect.IsEmpty());
  DCHECK(!bounds.IsEmpty());
  float scale = std::min(1.0f,
      std::min(static_cast<float>(bounds.width()) / rect.width(),
               static_cast<float>(bounds.height()) / rect.height()));
  return gfx::Rect(bounds.x() + 0.5 * (bounds.width() - scale * rect.width()),
                   bounds.y() + 0.5 * (bounds.height() - scale * rect.height()),
                   rect.width() * scale,
                   rect.height() * scale);
}

gfx::Transform ScopedTransformOverviewWindow::GetTransformForRect(
    const gfx::Rect& src_rect,
    const gfx::Rect& dst_rect) {
  DCHECK(!src_rect.IsEmpty());
  DCHECK(!dst_rect.IsEmpty());
  gfx::Transform transform;
  transform.Translate(dst_rect.x() - src_rect.x(),
                      dst_rect.y() - src_rect.y());
  transform.Scale(static_cast<float>(dst_rect.width()) / src_rect.width(),
                  static_cast<float>(dst_rect.height()) / src_rect.height());
  return transform;
}

void ScopedTransformOverviewWindow::SetTransform(
    aura::RootWindow* root_window,
    const gfx::Transform& transform,
    bool animate) {
  DCHECK(overview_started_);

  // If the window bounds have changed and a copy of the window is being
  // shown on another display, forcibly recreate the copy.
  if (window_copy_ && window_copy_->GetNativeWindow()->GetBoundsInScreen() !=
      window_->GetBoundsInScreen()) {
    DCHECK_NE(window_->GetRootWindow(), root_window);
    // TODO(flackr): If only the position changed and not the size, update the
    // existing window_copy_'s position and continue to use it.
    window_copy_->Close();
    if (layer_)
      views::corewm::DeepDeleteLayers(layer_);
    window_copy_ = NULL;
    layer_ = NULL;
  }

  if (root_window != window_->GetRootWindow() && !window_copy_) {
    DCHECK(!layer_);
    // TODO(flackr): Create copies of the transient children and transient
    // parent windows as well. Currently they will only be visible on the
    // window's initial display.
    layer_ = views::corewm::RecreateWindowLayers(window_, true);
    window_copy_ = CreateCopyOfWindow(root_window, window_, layer_);
  }
  SetTransformOnWindowAndTransientChildren(transform, animate);
}

void ScopedTransformOverviewWindow::SetTransformOnWindowAndTransientChildren(
    const gfx::Transform& transform,
    bool animate) {
  gfx::Point origin(GetBoundsInScreen().origin());
  aura::Window* window = window_;
  while (window->transient_parent())
    window = window->transient_parent();
  if (window_copy_) {
    SetTransformOnWindow(
        window_copy_->GetNativeWindow(),
        TranslateTransformOrigin(ScreenAsh::ConvertRectToScreen(
            window_->parent(), window_->GetTargetBounds()).origin() - origin,
            transform),
        animate);
  }
  SetTransformOnWindowAndAllTransientChildren(
      window,
      TranslateTransformOrigin(ScreenAsh::ConvertRectToScreen(
          window->parent(), window->GetTargetBounds()).origin() - origin,
          transform),
      animate);
}

void ScopedTransformOverviewWindow::PrepareForOverview() {
  DCHECK(!overview_started_);
  overview_started_ = true;
  ash::wm::GetWindowState(window_)->set_ignored_by_shelf(true);
  RestoreWindow();
}

}  // namespace ash
