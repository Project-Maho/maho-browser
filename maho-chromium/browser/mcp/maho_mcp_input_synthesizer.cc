// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_input_synthesizer.h"

#include <cctype>
#include <algorithm>
#include <cmath>
#include <string>

#ifndef MAHO_STANDALONE_TEST
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "components/input/native_web_keyboard_event.h"
#include "build/build_config.h"
#if BUILDFLAG(IS_MAC)
#include "content/browser/renderer_host/render_widget_host_impl.h"
#include "third_party/blink/public/mojom/input/input_handler.mojom.h"
#include "ui/latency/latency_info.h"
#endif
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/common/input/web_keyboard_event.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "third_party/blink/public/common/input/web_mouse_wheel_event.h"
#include "ui/accessibility/ax_node.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/events/keycodes/keyboard_code_conversion.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/event_constants.h"
#include "ui/gfx/geometry/point_conversions.h"
#include "ui/views/widget/widget.h"
#endif

namespace maho {

// static
std::string MahoMcpInputSynthesizer::NormalizeCrlf(const std::string& text) {
  std::string normalized;
  normalized.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\r') {
      if (i + 1 < text.size() && text[i + 1] == '\n') {
        ++i;
      }
      normalized.push_back('\n');
    } else {
      normalized.push_back(text[i]);
    }
  }
  return normalized;
}

#ifndef MAHO_STANDALONE_TEST
ClickMotionProfile::ClickMotionProfile() = default;
ClickMotionProfile::ClickMotionProfile(const ClickMotionProfile&) = default;
ClickMotionProfile::ClickMotionProfile(ClickMotionProfile&&) noexcept = default;
ClickMotionProfile& ClickMotionProfile::operator=(const ClickMotionProfile&) =
    default;
ClickMotionProfile& ClickMotionProfile::operator=(ClickMotionProfile&&) noexcept =
    default;
ClickMotionProfile::~ClickMotionProfile() = default;

namespace {

// pi is needed for the ease-in-out click trajectory; this Chromium tree's
// base/numerics/math_constants.h does not carry a pi constant.
constexpr double kPi = 3.14159265358979323846;

ui::AXNode* FindNodeById(ui::AXNode* root, ui::AXNodeID ax_id) {
  if (!root) return nullptr;
  if (root->id() == ax_id) return root;
  for (const auto& child : root->children()) {
    ui::AXNode* result = FindNodeById(child.get(), ax_id);
    if (result) return result;
  }
  return nullptr;
}

gfx::RectF GetAbsoluteBounds(ui::AXNode* node, ui::AXNode* root) {
  gfx::RectF bounds = node->data().relative_bounds.bounds;
  ui::AXNode* current = node;
  while (current && current != root) {
    ui::AXNodeID container_id = current->data().relative_bounds.offset_container_id;
    ui::AXNode* container = nullptr;
    if (container_id > 0) {
      container = FindNodeById(root, container_id);
    }
    if (!container) {
      container = current->parent();
    }
    if (container) {
      bounds.Offset(container->data().relative_bounds.bounds.OffsetFromOrigin());
    }
    current = container;
  }
  return bounds;
}

gfx::Rect GetNodeBounds(content::WebContents* wc, ui::AXNodeID ax_id) {
  ui::AXNode* root = wc->GetAccessibilityRootNode();
  if (!root) return gfx::Rect();
  ui::AXNode* node = FindNodeById(root, ax_id);
  if (!node) return gfx::Rect();
  
  gfx::RectF abs_rect = GetAbsoluteBounds(node, root);
  gfx::Rect view_rect = wc->GetRenderWidgetHostView()
                            ? wc->GetRenderWidgetHostView()->GetViewBounds()
                            : gfx::Rect();
  return gfx::Rect(view_rect.x() + abs_rect.x(),
                   view_rect.y() + abs_rect.y(),
                   abs_rect.width(),
                   abs_rect.height());
}

content::RenderWidgetHost* GetTargetRenderWidgetHost(content::WebContents* wc) {
  if (!wc) return nullptr;
  if (content::RenderFrameHost* focused = wc->GetFocusedFrame()) {
    if (content::RenderWidgetHost* rwh = focused->GetRenderWidgetHost()) {
      return rwh;
    }
  }
  content::RenderWidgetHostView* view = wc->GetRenderWidgetHostView();
  return view ? view->GetRenderWidgetHost() : nullptr;
}

// Focus the renderer only when its browser window is already active. Calling
// WebContents::Focus() for a background window would activate the application.
void EnsureFocusedForInput(content::WebContents* wc) {
  if (!wc) return;
  auto* widget = views::Widget::GetWidgetForNativeWindow(
      wc->GetTopLevelNativeWindow());
  if (!widget || !widget->IsActive()) return;
  content::RenderWidgetHostView* view = wc->GetRenderWidgetHostView();
  if (view && view->HasFocus()) return;
  wc->Focus();
}

blink::WebMouseEvent MakeMouseEvent(
    blink::WebInputEvent::Type type,
    int modifiers_mask,
    blink::WebMouseEvent::Button button,
    int click_count,
    const gfx::Point& widget_point,
    const gfx::Point& screen_point) {
  blink::WebMouseEvent event(type, modifiers_mask, base::TimeTicks::Now());
  event.button = button;
  event.click_count = click_count;
  event.SetPositionInWidget(widget_point.x(), widget_point.y());
  event.SetPositionInScreen(screen_point.x(), screen_point.y());
  return event;
}

}  // namespace

// static
bool MahoMcpInputSynthesizer::Scroll(content::WebContents* wc,
                                     const std::string& direction,
                                     int pixels,
                                     std::optional<ui::AXNodeID> ax_id) {
  EnsureFocusedForInput(wc);
  content::RenderWidgetHostView* view = wc->GetRenderWidgetHostView();
  if (!view) return false;
  content::RenderWidgetHost* rwh = view->GetRenderWidgetHost();
  if (!rwh) return false;

  gfx::Point widget_point(100, 100);
  gfx::Point screen_point(100, 100);

  if (ax_id.has_value()) {
    gfx::Rect bounds = GetNodeBounds(wc, ax_id.value());
    if (!bounds.IsEmpty()) {
      screen_point = bounds.CenterPoint();
      widget_point = screen_point - view->GetViewBounds().origin().OffsetFromOrigin();
    }
  }

  blink::WebMouseWheelEvent wheel_event(
      blink::WebInputEvent::Type::kMouseWheel,
      blink::WebInputEvent::kNoModifiers,
      base::TimeTicks::Now());
  wheel_event.SetPositionInWidget(widget_point.x(), widget_point.y());
  wheel_event.SetPositionInScreen(screen_point.x(), screen_point.y());

  int delta_x = 0;
  int delta_y = 0;
  if (direction == "down") delta_y = -pixels;
  else if (direction == "up") delta_y = pixels;
  else if (direction == "left") delta_x = pixels;
  else if (direction == "right") delta_x = -pixels;

  wheel_event.delta_x = delta_x;
  wheel_event.delta_y = delta_y;
  wheel_event.phase = blink::WebMouseWheelEvent::kPhaseBegan;

  rwh->ForwardWheelEvent(wheel_event);

  blink::WebMouseWheelEvent end_event = wheel_event;
  end_event.delta_x = 0;
  end_event.delta_y = 0;
  end_event.phase = blink::WebMouseWheelEvent::kPhaseEnded;
  rwh->ForwardWheelEvent(end_event);

  return true;
}

// static
bool MahoMcpInputSynthesizer::Hover(content::WebContents* wc, ui::AXNodeID ax_id) {
  EnsureFocusedForInput(wc);
  content::RenderWidgetHostView* view = wc->GetRenderWidgetHostView();
  if (!view) return false;
  content::RenderWidgetHost* rwh = view->GetRenderWidgetHost();
  if (!rwh) return false;

  gfx::Rect bounds = GetNodeBounds(wc, ax_id);
  if (bounds.IsEmpty()) return false;
  gfx::Point screen_point = bounds.CenterPoint();
  gfx::Point widget_point = screen_point - view->GetViewBounds().origin().OffsetFromOrigin();

  blink::WebMouseEvent mouse_event(
      blink::WebInputEvent::Type::kMouseMove,
      blink::WebInputEvent::kNoModifiers,
      base::TimeTicks::Now());
  mouse_event.SetPositionInWidget(widget_point.x(), widget_point.y());
  mouse_event.SetPositionInScreen(screen_point.x(), screen_point.y());
  
  rwh->ForwardMouseEvent(mouse_event);
  return true;
}

// static
bool MahoMcpInputSynthesizer::HoverAt(content::WebContents* wc,
                                      int x_css,
                                      int y_css,
                                      bool sensitive) {
  if (!wc) return false;
  EnsureFocusedForInput(wc);
  content::RenderWidgetHostView* view = wc->GetRenderWidgetHostView();
  if (!view) return false;
  content::RenderWidgetHost* rwh = view->GetRenderWidgetHost();
  if (!rwh) return false;
  content::RenderFrameHost* primary = wc->GetPrimaryMainFrame();
  if (!primary) return false;

  const content::GlobalRenderFrameHostId primary_frame_id =
      primary->GetGlobalId();
  gfx::Point widget_point(x_css, y_css);
  gfx::Point screen_point = widget_point + view->GetViewBounds().origin().OffsetFromOrigin();

  blink::WebMouseEvent mouse_event(
      blink::WebInputEvent::Type::kMouseMove,
      blink::WebInputEvent::kNoModifiers,
      base::TimeTicks::Now());
  mouse_event.SetPositionInWidget(widget_point.x(), widget_point.y());
  mouse_event.SetPositionInScreen(screen_point.x(), screen_point.y());
  
  rwh->ForwardMouseEvent(mouse_event);
  MahoActionMarkerService::ShowForRevalidatedTarget(
      wc, {.viewport_point = gfx::PointF(x_css, y_css),
           .kind = MahoActionMarkerService::Kind::kHover,
           .sensitive = sensitive,
           .primary_frame_id = primary_frame_id});
  return true;
}

// static
bool MahoMcpInputSynthesizer::ClickAt(
    content::WebContents* wc,
    int x_css,
    int y_css,
    const std::vector<std::string>& modifiers,
    bool sensitive,
    MahoActionMarkerService::Kind marker_kind) {
  if (!wc) return false;
  EnsureFocusedForInput(wc);
  content::RenderWidgetHostView* view = wc->GetRenderWidgetHostView();
  if (!view) return false;
  content::RenderWidgetHost* rwh = view->GetRenderWidgetHost();
  if (!rwh) return false;
  content::RenderFrameHost* primary = wc->GetPrimaryMainFrame();
  if (!primary) return false;

  const content::GlobalRenderFrameHostId primary_frame_id =
      primary->GetGlobalId();
  const int base_modifiers = ModifierListToWebModifiers(modifiers);
  gfx::Point widget_point(x_css, y_css);
  gfx::Point screen_point =
      widget_point + view->GetViewBounds().origin().OffsetFromOrigin();

  auto build = [&](blink::WebInputEvent::Type type, int modifiers_mask,
                   blink::WebMouseEvent::Button button, int click_count) {
    return MakeMouseEvent(type, modifiers_mask, button, click_count,
                          widget_point, screen_point);
  };

  // Move the pointer over the target first so hover/pointer state is correct,
  // then a trusted press + release. Blink expands this into the full
  // pointerdown -> mousedown -> pointerup -> mouseup -> click sequence.
  rwh->ForwardMouseEvent(build(blink::WebInputEvent::Type::kMouseMove,
                               base_modifiers,
                               blink::WebMouseEvent::Button::kNoButton, 0));
  rwh->ForwardMouseEvent(build(
      blink::WebInputEvent::Type::kMouseDown,
      base_modifiers | blink::WebInputEvent::kLeftButtonDown,
      blink::WebMouseEvent::Button::kLeft, 1));
  rwh->ForwardMouseEvent(build(blink::WebInputEvent::Type::kMouseUp,
                               base_modifiers,
                               blink::WebMouseEvent::Button::kLeft, 1));
  MahoActionMarkerService::ShowForRevalidatedTarget(
      wc, {.viewport_point = gfx::PointF(x_css, y_css),
           .kind = marker_kind,
           .sensitive = sensitive,
           .primary_frame_id = primary_frame_id});
  return true;
}

// static
double MahoMcpInputSynthesizer::EaseInOutFraction(double t) {
  if (t <= 0.0) return 0.0;
  if (t >= 1.0) return 1.0;
  return 0.5 * (1.0 - std::cos(kPi * t));
}

// static
std::vector<gfx::PointF> MahoMcpInputSynthesizer::ComputeTrajectory(
    const gfx::PointF& start,
    const gfx::PointF& end,
    int waypoint_count,
    int jitter_px,
    SplitMix64Prng& rng) {
  std::vector<gfx::PointF> points;
  waypoint_count = std::clamp(waypoint_count, 2, 64);
  points.reserve(static_cast<size_t>(waypoint_count));
  const float dx = end.x() - start.x();
  const float dy = end.y() - start.y();
  for (int i = 1; i <= waypoint_count; ++i) {
    if (i == waypoint_count) {
      points.push_back(end);
      break;
    }
    const double t =
        EaseInOutFraction(static_cast<double>(i) /
                          static_cast<double>(waypoint_count));
    float x = start.x() + static_cast<float>(dx * t);
    float y = start.y() + static_cast<float>(dy * t);
    if (jitter_px > 0) {
      x += static_cast<float>(rng.NextRange(-jitter_px, jitter_px));
      y += static_cast<float>(rng.NextRange(-jitter_px, jitter_px));
    }
    points.emplace_back(x, y);
  }
  return points;
}

MahoMcpInputSynthesizer::BoundedMotionPlan::BoundedMotionPlan() = default;
MahoMcpInputSynthesizer::BoundedMotionPlan::~BoundedMotionPlan() = default;
MahoMcpInputSynthesizer::BoundedMotionPlan::BoundedMotionPlan(
    const BoundedMotionPlan&) = default;
MahoMcpInputSynthesizer::BoundedMotionPlan&
MahoMcpInputSynthesizer::BoundedMotionPlan::operator=(
    const BoundedMotionPlan&) = default;
MahoMcpInputSynthesizer::BoundedMotionPlan::BoundedMotionPlan(
    BoundedMotionPlan&&) noexcept = default;
MahoMcpInputSynthesizer::BoundedMotionPlan&
MahoMcpInputSynthesizer::BoundedMotionPlan::operator=(
    BoundedMotionPlan&&) noexcept = default;

// static
double MahoMcpInputSynthesizer::NextGaussian(SplitMix64Prng& rng) {
  // Box-Muller transform from two uniform draws in [0, 1).
  constexpr double kEpsilon = 1e-12;
  const double u1 = std::max(
      kEpsilon,
      static_cast<double>(rng.NextU64() >> 11) / (1ull << 53));
  const double u2 =
      static_cast<double>(rng.NextU64() >> 11) / (1ull << 53);
  return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * kPi * u2);
}

// static
int32_t MahoMcpInputSynthesizer::NextDwellMs(
    const ClickMotionProfile& profile, SplitMix64Prng& rng) {
  // Log-normal centered near the geometric mean of the dwell window,
  // truncated to [min_ms, max_ms].
  const double lo = profile.dwell_ms.min_ms;
  const double hi = profile.dwell_ms.max_ms;
  const double mu = std::log(std::max(1.0, std::sqrt(lo * hi)));
  const double sigma = 0.35;
  const double sample = std::exp(mu + sigma * NextGaussian(rng));
  return static_cast<int32_t>(std::clamp(sample, lo, hi));
}

// static
MahoMcpInputSynthesizer::BoundedMotionPlan
MahoMcpInputSynthesizer::ComputeBoundedMotion(
    const gfx::PointF& start,
    const gfx::RectF& target_rect,
    const ClickMotionProfile& profile,
    SplitMix64Prng& rng) {
  BoundedMotionPlan plan;
  const gfx::PointF center(
      target_rect.x() + target_rect.width() / 2.0f,
      target_rect.y() + target_rect.height() / 2.0f);
  const float dx = center.x() - start.x();
  const float dy = center.y() - start.y();
  const float length = std::sqrt(dx * dx + dy * dy);
  // Unit perpendicular of the approach direction.
  const float perp_x = length > 0.0f ? -dy / length : 0.0f;
  const float perp_y = length > 0.0f ? dx / length : 1.0f;

  const int waypoint_count =
      std::clamp(profile.min_waypoints +
                     rng.NextRange(
                         0, std::max(1, profile.max_waypoints -
                                           profile.min_waypoints)),
                 2, 64);

  const double sigma = std::max(0.0, profile.approach_jitter_sigma_px);
  const double max_offset = sigma * 3.0;  // 3-sigma clamp

  auto clamp_to_rect = [&target_rect](const gfx::PointF& p) {
    return gfx::PointF(
        std::clamp(p.x(), target_rect.x(),
                   target_rect.x() + target_rect.width()),
        std::clamp(p.y(), target_rect.y(),
                   target_rect.y() + target_rect.height()));
  };

  // Approach waypoints: ease-in-out progress with bounded perpendicular
  // Gaussian jitter. These stay near the centerline (well inside page
  // content); the terminal points are handled below.
  for (int i = 1; i < waypoint_count; ++i) {
    const double t =
        EaseInOutFraction(static_cast<double>(i) /
                          static_cast<double>(waypoint_count));
    gfx::PointF point(start.x() + dx * static_cast<float>(t),
                      start.y() + dy * static_cast<float>(t));
    if (sigma > 0.0) {
      const double offset =
          std::clamp(NextGaussian(rng) * sigma, -max_offset, max_offset);
      point = gfx::PointF(
          point.x() + perp_x * static_cast<float>(offset),
          point.y() + perp_y * static_cast<float>(offset));
    }
    plan.waypoints.push_back(point);
  }

  // Overshoot: profile.overshoot_probability of motions carry a 2-4px
  // overshoot past the center along the approach direction, clamped inside
  // the target rect and followed by a correction back to the exact center.
  // Tiny targets disable overshoot entirely.
  const bool tiny_target =
      target_rect.width() < 8.0f || target_rect.height() < 8.0f;
  const double roll =
      static_cast<double>(rng.NextU64() >> 11) / (1ull << 53);
  const bool overshoot_allowed =
      profile.overshoot_probability > 0.0 && !tiny_target &&
      profile.overshoot_max_px >= profile.overshoot_min_px;
  plan.overshoot_applied =
      overshoot_allowed && length > 0.0f &&
      roll < profile.overshoot_probability;
  if (plan.overshoot_applied) {
    const int magnitude =
        rng.NextRange(profile.overshoot_min_px, profile.overshoot_max_px);
    const gfx::PointF past(
        center.x() + (dx / length) * static_cast<float>(magnitude),
        center.y() + (dy / length) * static_cast<float>(magnitude));
    plan.waypoints.push_back(clamp_to_rect(past));
  }

  // Correction/final waypoint: the exact target center.
  plan.waypoints.push_back(center);

  plan.dwell_ms = NextDwellMs(profile, rng);
  return plan;
}

// static
std::vector<TypingKeyUnit> MahoMcpInputSynthesizer::Utf8ToTypingUnits(
    const std::string& text) {
  const std::string normalized = NormalizeCrlf(text);
  std::u16string u16 = base::UTF8ToUTF16(normalized);
  std::vector<TypingKeyUnit> units;
  units.reserve(u16.size());
  for (size_t i = 0; i < u16.size(); ++i) {
    char16_t c1 = u16[i];
    TypingKeyUnit unit;
    if (c1 >= 0xD800 && c1 <= 0xDBFF && i + 1 < u16.size()) {
      char16_t c2 = u16[i + 1];
      if (c2 >= 0xDC00 && c2 <= 0xDFFF) {
        unit.code_point = 0x10000 +
                          ((static_cast<char32_t>(c1) - 0xD800) << 10) +
                          (static_cast<char32_t>(c2) - 0xDC00);
        unit.utf16.push_back(c1);
        unit.utf16.push_back(c2);
        unit.utf8 = base::UTF16ToUTF8(unit.utf16);
        units.push_back(std::move(unit));
        ++i;
        continue;
      }
    }
    unit.code_point = static_cast<char32_t>(c1);
    unit.utf16.push_back(c1);
    unit.utf8 = base::UTF16ToUTF8(unit.utf16);
    units.push_back(std::move(unit));
  }
  return units;
}

namespace {

std::vector<blink::WebKeyboardEvent> BuildWebKeyboardEventsForUnit(
    const TypingKeyUnit& unit,
    int web_modifiers) {
  std::vector<blink::WebKeyboardEvent> events;
  if (unit.code_point < 0x80) {
    char c = static_cast<char>(unit.code_point);
    if (c == '\n' || c == '\r') {
      ui::KeyboardCode key_code = ui::VKEY_RETURN;
      ui::DomCode dom_code = ui::DomCode::ENTER;
      int dom_key = static_cast<int>(ui::DomKey::ENTER);

      blink::WebKeyboardEvent key_down(blink::WebInputEvent::Type::kRawKeyDown,
                                       web_modifiers,
                                       base::TimeTicks::Now());
      key_down.windows_key_code = key_code;
      key_down.dom_code = static_cast<int>(dom_code);
      key_down.dom_key = dom_key;
      events.push_back(key_down);

      blink::WebKeyboardEvent key_char(blink::WebInputEvent::Type::kChar,
                                       web_modifiers,
                                       base::TimeTicks::Now());
      key_char.windows_key_code = key_code;
      key_char.dom_code = static_cast<int>(dom_code);
      key_char.dom_key = dom_key;
      key_char.text[0] = '\r';
      key_char.unmodified_text[0] = '\r';
      events.push_back(key_char);

      blink::WebKeyboardEvent key_up(blink::WebInputEvent::Type::kKeyUp,
                                     web_modifiers,
                                     base::TimeTicks::Now());
      key_up.windows_key_code = key_code;
      key_up.dom_code = static_cast<int>(dom_code);
      key_up.dom_key = dom_key;
      events.push_back(key_up);
      return events;
    }

    ui::KeyboardCode key_code = ui::VKEY_UNKNOWN;
    const char upper = static_cast<char>(std::toupper(
        static_cast<unsigned char>(c)));
    if (upper >= 'A' && upper <= 'Z') {
      key_code = static_cast<ui::KeyboardCode>(ui::VKEY_A + (upper - 'A'));
    } else if (c >= '0' && c <= '9') {
      key_code = static_cast<ui::KeyboardCode>(ui::VKEY_0 + (c - '0'));
    } else if (c == ' ') {
      key_code = ui::VKEY_SPACE;
    }
    const ui::DomCode dom_code = ui::UsLayoutKeyboardCodeToDomCode(key_code);
    const int dom_key =
        static_cast<int>(ui::DomKey::FromCharacter(unit.code_point));

    blink::WebKeyboardEvent key_down(blink::WebInputEvent::Type::kRawKeyDown,
                                     web_modifiers,
                                     base::TimeTicks::Now());
    key_down.windows_key_code = key_code;
    key_down.dom_code = static_cast<int>(dom_code);
    key_down.dom_key = dom_key;
    events.push_back(key_down);

    blink::WebKeyboardEvent key_char(blink::WebInputEvent::Type::kChar,
                                     web_modifiers,
                                     base::TimeTicks::Now());
    key_char.windows_key_code = key_code;
    key_char.dom_code = static_cast<int>(dom_code);
    key_char.dom_key = dom_key;
    key_char.text[0] = static_cast<char16_t>(c);
    key_char.unmodified_text[0] = static_cast<char16_t>(c);
    events.push_back(key_char);

    blink::WebKeyboardEvent key_up(blink::WebInputEvent::Type::kKeyUp,
                                   web_modifiers,
                                   base::TimeTicks::Now());
    key_up.windows_key_code = key_code;
    key_up.dom_code = static_cast<int>(dom_code);
    key_up.dom_key = dom_key;
    events.push_back(key_up);
    return events;
  }

  const int dom_key =
      static_cast<int>(ui::DomKey::FromCharacter(unit.code_point));

  blink::WebKeyboardEvent key_down(blink::WebInputEvent::Type::kRawKeyDown,
                                   web_modifiers,
                                   base::TimeTicks::Now());
  key_down.windows_key_code = 0;
  key_down.dom_code = 0;
  key_down.dom_key = dom_key;
  events.push_back(key_down);

  blink::WebKeyboardEvent key_char(blink::WebInputEvent::Type::kChar,
                                   web_modifiers,
                                   base::TimeTicks::Now());
  key_char.windows_key_code = 0;
  key_char.dom_code = 0;
  key_char.dom_key = dom_key;
  for (size_t k = 0;
       k < unit.utf16.size() &&
       k < blink::WebKeyboardEvent::kTextLengthCap;
       ++k) {
    key_char.text[k] = unit.utf16[k];
    key_char.unmodified_text[k] = unit.utf16[k];
  }
  events.push_back(key_char);

  blink::WebKeyboardEvent key_up(blink::WebInputEvent::Type::kKeyUp,
                                 web_modifiers,
                                 base::TimeTicks::Now());
  key_up.windows_key_code = 0;
  key_up.dom_code = 0;
  key_up.dom_key = dom_key;
  events.push_back(key_up);
  return events;
}

}  // namespace

// static
bool MahoMcpInputSynthesizer::TypeText(content::WebContents* wc,
                                       const std::string& text) {
  EnsureFocusedForInput(wc);
  content::RenderWidgetHost* rwh = GetTargetRenderWidgetHost(wc);
  if (!rwh) return false;

  std::vector<TypingKeyUnit> units = Utf8ToTypingUnits(text);
  for (const auto& unit : units) {
    std::vector<blink::WebKeyboardEvent> events =
        BuildWebKeyboardEventsForUnit(unit, 0);
    for (const auto& event : events) {
      rwh->ForwardKeyboardEvent(
          input::NativeWebKeyboardEvent(event, wc->GetNativeView()));
    }
  }
  return true;
}

namespace {

class AsyncTypingSession {
 public:
  AsyncTypingSession(base::WeakPtr<content::WebContents> wc,
                     std::string text,
                     TypingPacingPolicy policy,
                     base::OnceCallback<void(bool)> callback)
      : web_contents_(std::move(wc)),
        units_(MahoMcpInputSynthesizer::Utf8ToTypingUnits(text)),
        policy_(std::move(policy)),
        prng_(policy_.CreatePrng()),
        callback_(std::move(callback)) {}

  void Start() {
    Step();
  }

 private:
  void Step() {
    if (!web_contents_) {
      Finish(false);
      return;
    }

    if (index_ >= units_.size()) {
      Finish(true);
      return;
    }

    const auto& current_unit = units_[index_];
    if (!MahoMcpInputSynthesizer::TypeText(web_contents_.get(),
                                           current_unit.utf8)) {
      Finish(false);
      return;
    }

    index_++;
    if (index_ >= units_.size()) {
      Finish(true);
      return;
    }

    char32_t prev_cp = current_unit.code_point;
    char32_t next_cp = units_[index_].code_point;
    int32_t delay_ms = policy_.delay_for(prev_cp, next_cp, prng_);

    if (delay_ms <= 0) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(&AsyncTypingSession::Step, weak_factory_.GetWeakPtr()));
    } else {
      base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&AsyncTypingSession::Step, weak_factory_.GetWeakPtr()),
          base::Milliseconds(delay_ms));
    }
  }

  void Finish(bool success) {
    if (callback_) {
      std::move(callback_).Run(success);
    }
    delete this;
  }

  base::WeakPtr<content::WebContents> web_contents_;
  std::vector<TypingKeyUnit> units_;
  TypingPacingPolicy policy_;
  SplitMix64Prng prng_;
  size_t index_{0};
  base::OnceCallback<void(bool)> callback_;
  base::WeakPtrFactory<AsyncTypingSession> weak_factory_{this};
};

}  // namespace

void MahoMcpInputSynthesizer::TypeTextAsync(
    content::WebContents* wc,
    const std::string& text,
    TypingOptions options,
    base::OnceCallback<void(bool)> callback) {
  if (!options.pacing_policy.validate()) {
    if (base::SequencedTaskRunner::HasCurrentDefault()) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(
              [](base::OnceCallback<void(bool)> done) {
                std::move(done).Run(false);
              },
              std::move(callback)));
    } else {
      std::move(callback).Run(false);
    }
    return;
  }

  if (text.empty()) {
    if (base::SequencedTaskRunner::HasCurrentDefault()) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(
              [](base::OnceCallback<void(bool)> done) {
                std::move(done).Run(true);
              },
              std::move(callback)));
    } else {
      std::move(callback).Run(true);
    }
    return;
  }

  if (!base::SequencedTaskRunner::HasCurrentDefault()) {
    const bool dispatched = TypeText(wc, text);
    std::move(callback).Run(dispatched);
    return;
  }

  auto* session = new AsyncTypingSession(
      wc ? wc->GetWeakPtr() : nullptr,
      text,
      options.pacing_policy,
      std::move(callback));
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](AsyncTypingSession* pending_session) {
            pending_session->Start();
          },
          base::Unretained(session)));
}

namespace {

// Drives one trusted click as a timed sequence: interpolated ease-in-out
// pointer travel, a pre-click dwell, and a press-to-release gap, followed by
// completion reporting. Strictly bounded by ClickMotionProfile caps.
class AsyncClickSession {
 public:
  struct Params {
    raw_ptr<content::RenderWidgetHost> rwh;
    gfx::Point widget_point;
    gfx::Point screen_point;
    int base_modifiers = 0;
    std::optional<content::GlobalRenderFrameHostId> primary_frame_id;
    std::optional<gfx::PointF> marker_viewport_point;
    MahoActionMarkerService::Kind marker_kind =
        MahoActionMarkerService::Kind::kClick;
    bool marker_sensitive = true;
  };

  AsyncClickSession(
      Params params,
      base::WeakPtr<content::WebContents> web_contents,
      const ClickMotionProfile& profile,
      base::OnceCallback<void(bool)> callback)
      : params_(std::move(params)),
        web_contents_(std::move(web_contents)),
        profile_(profile),
        prng_(profile.rng_seed.value_or(0x94d049bb133111ebULL)),
        callback_(std::move(callback)) {}

  void Start() {
    if (!params_.rwh) {
      Finish(false);
      return;
    }
    // Pointer travel begins from a bounded random approach vector; the exact
    // prior cursor position is not tracked.
    const int approach_mag = prng_.NextRange(60, 140);
    const double angle_rad =
        static_cast<double>(prng_.NextRange(0, 359)) * kPi /
        180.0;
    const gfx::PointF target(
        static_cast<float>(params_.widget_point.x()),
        static_cast<float>(params_.widget_point.y()));
    const gfx::PointF start(
        target.x() - static_cast<float>(approach_mag * std::cos(angle_rad)),
        target.y() - static_cast<float>(approach_mag * std::sin(angle_rad)));
    const int waypoint_count =
        prng_.NextRange(profile_.min_waypoints, profile_.max_waypoints);
    waypoints_ = MahoMcpInputSynthesizer::ComputeTrajectory(
        start, target, waypoint_count, profile_.approach_jitter_px, prng_);
    const int total_ms = prng_.NextRange(profile_.total_duration_ms.min_ms,
                                         profile_.total_duration_ms.max_ms);
    step_delay_ = base::Milliseconds(
        std::max(1, total_ms / static_cast<int>(waypoints_.size())));
    AdvancePointer();
  }

 private:
  void PostDelayed(base::TimeDelta delay, base::OnceClosure closure) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, std::move(closure), delay);
  }

  void AdvancePointer() {
    // Deliberately no weak_factory_.HasWeakPtrs() guard here. Start() invokes
    // AdvancePointer() directly before any weak pointer has been handed out,
    // so such a guard always early-returned: no mouse events were forwarded,
    // Finish() never ran, and the completion callback never fired, hanging
    // async click/type callers until they timed out. Lifetime safety is
    // already provided by binding every continuation below through
    // weak_factory_.GetWeakPtr() - pending steps are dropped if this session
    // is destroyed.
    if (index_ >= waypoints_.size()) {
      PostDelayed(
          base::Milliseconds(
              prng_.NextRange(profile_.pre_click_dwell_ms.min_ms,
                              profile_.pre_click_dwell_ms.max_ms)),
          base::BindOnce(&AsyncClickSession::DispatchPress,
                         weak_factory_.GetWeakPtr()));
      return;
    }
    const gfx::PointF p = waypoints_[index_++];
    params_.rwh->ForwardMouseEvent(MakeMouseEvent(
        blink::WebInputEvent::Type::kMouseMove, params_.base_modifiers,
        blink::WebMouseEvent::Button::kNoButton, 0,
        gfx::ToRoundedPoint(p), params_.screen_point));
    PostDelayed(
        step_delay_,
        base::BindOnce(&AsyncClickSession::AdvancePointer,
                       weak_factory_.GetWeakPtr()));
  }

  void DispatchPress() {
    params_.rwh->ForwardMouseEvent(MakeMouseEvent(
        blink::WebInputEvent::Type::kMouseDown,
        params_.base_modifiers | blink::WebInputEvent::kLeftButtonDown,
        blink::WebMouseEvent::Button::kLeft, 1, params_.widget_point,
        params_.screen_point));
    PostDelayed(
        base::Milliseconds(prng_.NextRange(profile_.press_release_ms.min_ms,
                                           profile_.press_release_ms.max_ms)),
        base::BindOnce(&AsyncClickSession::DispatchRelease,
                       weak_factory_.GetWeakPtr()));
  }

  void DispatchRelease() {
    params_.rwh->ForwardMouseEvent(MakeMouseEvent(
        blink::WebInputEvent::Type::kMouseUp, params_.base_modifiers,
        blink::WebMouseEvent::Button::kLeft, 1, params_.widget_point,
        params_.screen_point));
    if (web_contents_ && params_.marker_viewport_point &&
        params_.primary_frame_id) {
      MahoActionMarkerService::ShowForRevalidatedTarget(
          web_contents_.get(),
          {.viewport_point = *params_.marker_viewport_point,
           .kind = params_.marker_kind,
           .sensitive = params_.marker_sensitive,
           .primary_frame_id = *params_.primary_frame_id});
    }
    Finish(true);
  }

  void Finish(bool ok) {
    std::move(callback_).Run(ok);
    delete this;
  }

  Params params_;
  base::WeakPtr<content::WebContents> web_contents_;
  ClickMotionProfile profile_;
  SplitMix64Prng prng_;
  std::vector<gfx::PointF> waypoints_;
  size_t index_ = 0;
  base::TimeDelta step_delay_;
  base::OnceCallback<void(bool)> callback_;
  base::WeakPtrFactory<AsyncClickSession> weak_factory_{this};
};

}  // namespace

// static
void MahoMcpInputSynthesizer::ClickAtAsync(
    content::WebContents* wc,
    int x_css,
    int y_css,
    const std::vector<std::string>& modifiers,
    bool sensitive,
    MahoActionMarkerService::Kind marker_kind,
    const ClickMotionProfile& profile,
    base::OnceCallback<void(bool)> callback) {
  if (!profile.validate()) {
    if (base::SequencedTaskRunner::HasCurrentDefault()) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(std::move(callback), false));
    } else {
      std::move(callback).Run(false);
    }
    return;
  }
  if (!base::SequencedTaskRunner::HasCurrentDefault()) {
    // No UI-sequenced task runner (e.g. plain unit-test context): fall back to
    // synchronous dispatch so behavior stays well-defined.
    const bool ok =
        ClickAt(wc, x_css, y_css, modifiers, sensitive, marker_kind);
    std::move(callback).Run(ok);
    return;
  }

  EnsureFocusedForInput(wc);
  content::RenderWidgetHostView* view =
      wc ? wc->GetRenderWidgetHostView() : nullptr;
  content::RenderWidgetHost* rwh =
      view ? view->GetRenderWidgetHost() : nullptr;
  content::RenderFrameHost* primary = wc ? wc->GetPrimaryMainFrame() : nullptr;
  if (!rwh || !primary) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), false));
    return;
  }

  AsyncClickSession::Params params;
  params.rwh = rwh;
  params.widget_point = gfx::Point(x_css, y_css);
  params.screen_point =
      params.widget_point + view->GetViewBounds().origin().OffsetFromOrigin();
  params.base_modifiers = ModifierListToWebModifiers(modifiers);
  params.primary_frame_id = primary->GetGlobalId();
  params.marker_viewport_point = gfx::PointF(x_css, y_css);
  params.marker_kind = marker_kind;
  params.marker_sensitive = sensitive;

  auto* session =
      new AsyncClickSession(std::move(params), wc->GetWeakPtr(), profile,
                            std::move(callback));
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](AsyncClickSession* pending_session) {
            pending_session->Start();
          },
          base::Unretained(session)));
}
#endif

#ifdef MAHO_STANDALONE_TEST
// static
std::vector<TypingKeyUnit> MahoMcpInputSynthesizer::Utf8ToTypingUnits(
    const std::string& text) {
  const std::string normalized = NormalizeCrlf(text);
  std::vector<TypingKeyUnit> units;
  size_t i = 0;
  while (i < normalized.size()) {
    unsigned char c = static_cast<unsigned char>(normalized[i]);
    if (c < 0x80) {
      TypingKeyUnit unit;
      unit.code_point = c;
      unit.utf16.push_back(static_cast<char16_t>(c));
      unit.utf8 = normalized.substr(i, 1);
      units.push_back(std::move(unit));
      ++i;
      continue;
    }

    char32_t cp = 0;
    size_t extra = 0;
    if ((c & 0xE0) == 0xC0) {
      cp = c & 0x1F;
      extra = 1;
    } else if ((c & 0xF0) == 0xE0) {
      cp = c & 0x0F;
      extra = 2;
    } else if ((c & 0xF8) == 0xF0) {
      cp = c & 0x07;
      extra = 3;
    } else {
      TypingKeyUnit unit;
      unit.code_point = c;
      unit.utf16.push_back(static_cast<char16_t>(c));
      unit.utf8 = text.substr(i, 1);
      units.push_back(std::move(unit));
      ++i;
      continue;
    }

    if (i + extra >= text.size()) {
      TypingKeyUnit unit;
      unit.code_point = c;
      unit.utf16.push_back(static_cast<char16_t>(c));
      unit.utf8 = text.substr(i, 1);
      units.push_back(std::move(unit));
      ++i;
      continue;
    }

    bool valid = true;
    for (size_t j = 1; j <= extra; ++j) {
      unsigned char next = static_cast<unsigned char>(text[i + j]);
      if ((next & 0xC0) != 0x80) {
        valid = false;
        break;
      }
      cp = (cp << 6) | (next & 0x3F);
    }

    if (!valid) {
      TypingKeyUnit unit;
      unit.code_point = c;
      unit.utf16.push_back(static_cast<char16_t>(c));
      unit.utf8 = text.substr(i, 1);
      units.push_back(std::move(unit));
      ++i;
      continue;
    }

    TypingKeyUnit unit;
    unit.code_point = cp;
    unit.utf8 = text.substr(i, 1 + extra);
    if (cp <= 0xFFFF) {
      unit.utf16.push_back(static_cast<char16_t>(cp));
    } else if (cp <= 0x10FFFF) {
      unit.utf16.push_back(
          static_cast<char16_t>(0xD800 + ((cp - 0x10000) >> 10)));
      unit.utf16.push_back(
          static_cast<char16_t>(0xDC00 + ((cp - 0x10000) & 0x3FF)));
    }
    units.push_back(std::move(unit));
    i += 1 + extra;
  }
  return units;
}
#endif

// static
std::string MahoMcpInputSynthesizer::MacEditCommandForKeyPress(
    ui::KeyboardCode key_code, int web_modifiers) {
  if (key_code == ui::VKEY_A &&
      web_modifiers == blink::WebInputEvent::kMetaKey) {
    return "SelectAll";
  }
  return {};
}

// static
ui::KeyboardCode MahoMcpInputSynthesizer::KeyStringToKeyCode(
    const std::string& key) {
  if (key == "Enter") return ui::VKEY_RETURN;
  if (key == "Tab") return ui::VKEY_TAB;
  if (key == "Escape") return ui::VKEY_ESCAPE;
  if (key == "Backspace") return ui::VKEY_BACK;
  if (key == "Delete") return ui::VKEY_DELETE;
  if (key == "ArrowUp") return ui::VKEY_UP;
  if (key == "ArrowDown") return ui::VKEY_DOWN;
  if (key == "ArrowLeft") return ui::VKEY_LEFT;
  if (key == "ArrowRight") return ui::VKEY_RIGHT;
  if (key == "Home") return ui::VKEY_HOME;
  if (key == "End") return ui::VKEY_END;
  if (key == "PageUp") return ui::VKEY_PRIOR;
  if (key == "PageDown") return ui::VKEY_NEXT;
  if (key == " " || key == "Space") return ui::VKEY_SPACE;
  if (key == "F1") return ui::VKEY_F1;
  if (key == "F2") return ui::VKEY_F2;
  if (key == "F3") return ui::VKEY_F3;
  if (key == "F4") return ui::VKEY_F4;
  if (key == "F5") return ui::VKEY_F5;
  if (key == "F6") return ui::VKEY_F6;
  if (key == "F7") return ui::VKEY_F7;
  if (key == "F8") return ui::VKEY_F8;
  if (key == "F9") return ui::VKEY_F9;
  if (key == "F10") return ui::VKEY_F10;
  if (key == "F11") return ui::VKEY_F11;
  if (key == "F12") return ui::VKEY_F12;
  if (key.size() == 1) {
    char c = std::toupper(key[0]);
    if (c >= 'A' && c <= 'Z') {
      return static_cast<ui::KeyboardCode>(ui::VKEY_A + (c - 'A'));
    } else if (c >= '0' && c <= '9') {
      return static_cast<ui::KeyboardCode>(ui::VKEY_0 + (c - '0'));
    }
  }
  return ui::VKEY_UNKNOWN;
}

// static
int MahoMcpInputSynthesizer::ModifierListToWebModifiers(
    const std::vector<std::string>& modifiers) {
  int web_modifiers = blink::WebInputEvent::kNoModifiers;
  for (const auto& mod : modifiers) {
    if (mod == "shift") web_modifiers |= blink::WebInputEvent::kShiftKey;
    else if (mod == "ctrl") web_modifiers |= blink::WebInputEvent::kControlKey;
    else if (mod == "alt") web_modifiers |= blink::WebInputEvent::kAltKey;
    else if (mod == "meta") web_modifiers |= blink::WebInputEvent::kMetaKey;
  }
  return web_modifiers;
}

#ifndef MAHO_STANDALONE_TEST
// static
bool MahoMcpInputSynthesizer::KeyPress(content::WebContents* wc,
                                       const std::string& key,
                                       const std::vector<std::string>& modifiers) {
  EnsureFocusedForInput(wc);
  content::RenderWidgetHost* rwh = GetTargetRenderWidgetHost(wc);
  if (!rwh) return false;

  ui::KeyboardCode key_code = KeyStringToKeyCode(key);
  int web_modifiers = ModifierListToWebModifiers(modifiers);

  ui::DomCode dom_code = ui::UsLayoutKeyboardCodeToDomCode(key_code);
  
  int event_flags = 0;
  if (web_modifiers & blink::WebInputEvent::kShiftKey) event_flags |= ui::EF_SHIFT_DOWN;
  if (web_modifiers & blink::WebInputEvent::kControlKey) event_flags |= ui::EF_CONTROL_DOWN;
  if (web_modifiers & blink::WebInputEvent::kAltKey) event_flags |= ui::EF_ALT_DOWN;
  if (web_modifiers & blink::WebInputEvent::kMetaKey) event_flags |= ui::EF_COMMAND_DOWN;

  ui::DomKey dom_key;
  ui::KeyboardCode unused_key_code;
  if (!ui::DomCodeToUsLayoutDomKey(dom_code, event_flags, &dom_key, &unused_key_code)) {
    if (key.size() == 1) {
      dom_key = ui::DomKey::FromCharacter(key[0]);
    } else {
      dom_key = ui::DomKey::UNIDENTIFIED;
    }
  }

  // RawKeyDown
  base::WeakPtr<content::WebContents> weak_wc = wc->GetWeakPtr();
  blink::WebKeyboardEvent key_down(
      blink::WebInputEvent::Type::kRawKeyDown, web_modifiers,
      base::TimeTicks::Now());
  key_down.windows_key_code = key_code;
  key_down.dom_code = static_cast<int>(dom_code);
  key_down.dom_key = static_cast<int>(dom_key);
  std::string edit_command;
#if BUILDFLAG(IS_MAC)
  edit_command = MacEditCommandForKeyPress(key_code, web_modifiers);
  if (!edit_command.empty()) {
    // This path does not pass through Cocoa's interpretKeyEvents/menu handling.
    // Attach the command to the keydown so preventDefault still cancels it.
    std::vector<blink::mojom::EditCommandPtr> commands;
    commands.push_back(blink::mojom::EditCommand::New(edit_command, ""));
    content::RenderWidgetHostImpl::From(rwh)->ForwardKeyboardEventWithCommands(
        input::NativeWebKeyboardEvent(key_down, wc->GetNativeView()),
        ui::LatencyInfo(), std::move(commands));
  } else
#endif
  {
    rwh->ForwardKeyboardEvent(
        input::NativeWebKeyboardEvent(key_down, wc->GetNativeView()));
  }

  if (!weak_wc || !weak_wc->GetRenderWidgetHostView()) {
    return true;
  }
  content::RenderWidgetHost* live_rwh =
      weak_wc->GetRenderWidgetHostView()->GetRenderWidgetHost();
  if (!live_rwh) {
    return true;
  }

  // Editing shortcuts do not insert a character.
  if (edit_command.empty() && (key.size() == 1 || key == "Enter")) {
    blink::WebKeyboardEvent key_char(
        blink::WebInputEvent::Type::kChar, web_modifiers,
        base::TimeTicks::Now());
    key_char.windows_key_code = key_code;
    key_char.dom_code = static_cast<int>(dom_code);
    key_char.dom_key = static_cast<int>(dom_key);
    if (key == "Enter") {
      key_char.text[0] = '\r';
      key_char.unmodified_text[0] = '\r';
    } else {
      key_char.text[0] = key[0];
      key_char.unmodified_text[0] = key[0];
    }
    live_rwh->ForwardKeyboardEvent(
        input::NativeWebKeyboardEvent(key_char, weak_wc->GetNativeView()));
    if (!weak_wc || !weak_wc->GetRenderWidgetHostView()) {
      return true;
    }
    live_rwh = weak_wc->GetRenderWidgetHostView()->GetRenderWidgetHost();
    if (!live_rwh) {
      return true;
    }
  }

  // KeyUp
  blink::WebKeyboardEvent key_up(
      blink::WebInputEvent::Type::kKeyUp, web_modifiers,
      base::TimeTicks::Now());
  key_up.windows_key_code = key_code;
  key_up.dom_code = static_cast<int>(dom_code);
  key_up.dom_key = static_cast<int>(dom_key);
  live_rwh->ForwardKeyboardEvent(
      input::NativeWebKeyboardEvent(key_up, weak_wc->GetNativeView()));

  return true;
}

VisualFrame::VisualFrame() = default;
VisualFrame::VisualFrame(const VisualFrame&) = default;
VisualFrame& VisualFrame::operator=(const VisualFrame&) = default;
VisualFrame::VisualFrame(VisualFrame&&) noexcept = default;
VisualFrame& VisualFrame::operator=(VisualFrame&&) noexcept = default;
VisualFrame::~VisualFrame() = default;

// static
std::optional<gfx::PointF> MahoMcpInputSynthesizer::CssToScreenDip(
    const VisualFrame& frame,
    const gfx::PointF& css) {
  // 1. Validate token and epochs
  if (frame.token.empty() || frame.is_stale) {
    return std::nullopt;
  }
  if (frame.live_transform_generation.has_value() &&
      *frame.live_transform_generation != frame.view_transform_generation) {
    return std::nullopt;
  }
  if (frame.live_document_epoch.has_value() &&
      *frame.live_document_epoch != frame.document_epoch) {
    return std::nullopt;
  }

  // 2. Reject moved window if live bounds are known
  if (frame.live_view_bounds.has_value() &&
      *frame.live_view_bounds != frame.view_bounds_in_screen) {
    return std::nullopt;
  }

  // 3. Validate numeric inputs
  if (!std::isfinite(css.x()) || !std::isfinite(css.y())) {
    return std::nullopt;
  }
  if (frame.viewport_css_size.width() <= 0.0f ||
      frame.viewport_css_size.height() <= 0.0f) {
    return std::nullopt;
  }
  if (frame.bitmap_size.width() <= 0 || frame.bitmap_size.height() <= 0) {
    return std::nullopt;
  }
  if (frame.page_zoom_factor <= 0.0f || !std::isfinite(frame.page_zoom_factor) ||
      frame.page_scale_factor <= 0.0f || !std::isfinite(frame.page_scale_factor)) {
    return std::nullopt;
  }

  // 4. Verify CSS point is within viewport bounds
  if (css.x() < 0.0f || css.y() < 0.0f ||
      css.x() > frame.viewport_css_size.width() ||
      css.y() > frame.viewport_css_size.height()) {
    return std::nullopt;
  }

  // 5. Verify captured bitmap-to-CSS mapping
  const double scale_x = static_cast<double>(frame.bitmap_size.width()) /
                         static_cast<double>(frame.viewport_css_size.width());
  const double scale_y = static_cast<double>(frame.bitmap_size.height()) /
                         static_cast<double>(frame.viewport_css_size.height());
  if (scale_x <= 0.0 || scale_y <= 0.0) {
    return std::nullopt;
  }
  // Aspect ratio consistency check (pixel aspect ratio tolerance within 5%)
  const double scale_diff = std::abs(scale_x - scale_y);
  if (scale_diff / std::max(scale_x, scale_y) > 0.05) {
    return std::nullopt;
  }

  // 6. Generate page zoom / visual viewport transform
  const float rel_x = css.x() - frame.visual_viewport_offset.x();
  const float rel_y = css.y() - frame.visual_viewport_offset.y();

  if (rel_x < 0.0f || rel_y < 0.0f) {
    return std::nullopt;
  }

  const float effective_zoom = frame.page_zoom_factor * frame.page_scale_factor;
  const float view_dip_x = rel_x * effective_zoom;
  const float view_dip_y = rel_y * effective_zoom;

  if (!frame.view_bounds_in_screen.IsEmpty()) {
    if (view_dip_x > frame.view_bounds_in_screen.width() + 0.5f ||
        view_dip_y > frame.view_bounds_in_screen.height() + 0.5f) {
      return std::nullopt;
    }
  }

  // 7. Add view origin from GetViewBounds
  const gfx::Point view_origin = frame.view_bounds_in_screen.origin();
  return gfx::PointF(view_origin.x() + view_dip_x,
                     view_origin.y() + view_dip_y);
}

// static
std::optional<NativePoint> MahoMcpInputSynthesizer::NormalizeWindowsNativePoint(
    const gfx::PointF& screen_dip,
    const gfx::Point& pixel_point,
    const gfx::Rect& virtual_screen) {
  if (virtual_screen.width() <= 1 || virtual_screen.height() <= 1) {
    return std::nullopt;
  }
  NativePoint pt;
  pt.screen_dip = screen_dip;
  pt.win_pixel_point = pixel_point;

  const int origin_x = virtual_screen.x();
  const int origin_y = virtual_screen.y();
  const int extent_x = virtual_screen.width();
  const int extent_y = virtual_screen.height();

  const int clamped_x =
      std::clamp(pixel_point.x(), origin_x, origin_x + extent_x - 1);
  const int clamped_y =
      std::clamp(pixel_point.y(), origin_y, origin_y + extent_y - 1);

  pt.win_normalized_x = static_cast<int>(
      (static_cast<int64_t>(clamped_x - origin_x) * 65535) / (extent_x - 1));
  pt.win_normalized_y = static_cast<int>(
      (static_cast<int64_t>(clamped_y - origin_y) * 65535) / (extent_y - 1));
  pt.win_flags =
      kMouseEventFVirtualDesk | kMouseEventFAbsolute | kMouseEventFMove;
  return pt;
}

// static
std::optional<NativePoint>
MahoMcpInputSynthesizer::ResolveNativePointWinForTesting(
    const VisualFrame& frame,
    const gfx::PointF& css,
    const gfx::Point& pixel_point,
    const gfx::Rect& virtual_screen) {
  std::optional<gfx::PointF> screen_dip = CssToScreenDip(frame, css);
  if (!screen_dip.has_value()) {
    return std::nullopt;
  }
  return NormalizeWindowsNativePoint(*screen_dip, pixel_point, virtual_screen);
}

// static
std::optional<NativePoint> MahoMcpInputSynthesizer::ResolveNativePoint(
    const VisualFrame& frame,
    const gfx::PointF& css) {
  std::optional<gfx::PointF> screen_dip = CssToScreenDip(frame, css);
  if (!screen_dip.has_value()) {
    return std::nullopt;
  }

#if BUILDFLAG(IS_MAC)
  return ResolveNativePointMac(frame, *screen_dip);
#elif BUILDFLAG(IS_WIN)
  return ResolveNativePointWin(frame, *screen_dip);
#else
  // Non-mac/non-win platforms: ResolveNativePoint returns nullopt
  return std::nullopt;
#endif
}

// static
std::optional<NativePoint> MahoMcpInputSynthesizer::ResolveNativePoint(
    content::WebContents* wc,
    const VisualFrame& frame,
    const gfx::PointF& css) {
  if (!wc) {
    return std::nullopt;
  }
  content::RenderWidgetHostView* view = wc->GetRenderWidgetHostView();
  if (!view) {
    return std::nullopt;
  }
  const gfx::Rect current_bounds = view->GetViewBounds();
  if (current_bounds != frame.view_bounds_in_screen) {
    return std::nullopt;
  }
  VisualFrame validated = frame;
  validated.live_view_bounds = current_bounds;
  return ResolveNativePoint(validated, css);
}

namespace {
std::optional<NativeInputAvailability>& NativeInputAvailabilityOverride() {
  static base::NoDestructor<std::optional<NativeInputAvailability>> override;
  return *override;
}
}  // namespace

// static
void MahoMcpInputSynthesizer::SetNativeInputAvailabilityOverrideForTesting(
    std::optional<NativeInputAvailability> override_val) {
  NativeInputAvailabilityOverride() = std::move(override_val);
}

// static
NativeInputAvailability MahoMcpInputSynthesizer::CheckNativeInputAvailability() {
  if (NativeInputAvailabilityOverride().has_value()) {
    return *NativeInputAvailabilityOverride();
  }
#if BUILDFLAG(IS_MAC)
  return CheckNativeInputAvailabilityMac();
#elif BUILDFLAG(IS_WIN)
  return CheckNativeInputAvailabilityWin();
#else
  NativeInputAvailability result;
  result.available = false;
  result.reason =
      "native input synthesis unsupported on linux; available = false";
  result.raw_os_status = 0;
  return result;
#endif
}

NativeEvent::NativeEvent() = default;
NativeEvent::~NativeEvent() = default;
NativeEvent::NativeEvent(const NativeEvent&) = default;
NativeEvent& NativeEvent::operator=(const NativeEvent&) = default;
NativeEvent::NativeEvent(NativeEvent&&) noexcept = default;
NativeEvent& NativeEvent::operator=(NativeEvent&&) noexcept = default;

// static
NativeEvent NativeEvent::MakeMouseMove(const NativePoint& pt) {
  NativeEvent ev;
  ev.type = Type::kMouseMove;
  ev.point = pt;
  return ev;
}

// static
NativeEvent NativeEvent::MakeMouseDown(const NativePoint& pt,
                                      MouseButton btn) {
  NativeEvent ev;
  ev.type = Type::kMouseDown;
  ev.point = pt;
  ev.button = btn;
  return ev;
}

// static
NativeEvent NativeEvent::MakeMouseUp(const NativePoint& pt,
                                    MouseButton btn) {
  NativeEvent ev;
  ev.type = Type::kMouseUp;
  ev.point = pt;
  ev.button = btn;
  return ev;
}

// static
NativeEvent NativeEvent::MakeKeyDown(ui::KeyboardCode code,
                                    uint32_t scan,
                                    uint32_t mods) {
  NativeEvent ev;
  ev.type = Type::kKeyDown;
  ev.key_code = code;
  ev.scan_code = scan;
  ev.modifiers = mods;
  return ev;
}

// static
NativeEvent NativeEvent::MakeKeyUp(ui::KeyboardCode code,
                                  uint32_t scan,
                                  uint32_t mods) {
  NativeEvent ev;
  ev.type = Type::kKeyUp;
  ev.key_code = code;
  ev.scan_code = scan;
  ev.modifiers = mods;
  return ev;
}

// static
NativeEvent NativeEvent::MakeKeyChar(char32_t cp,
                                    const std::u16string& u16_text,
                                    uint32_t mods) {
  NativeEvent ev;
  ev.type = Type::kKeyChar;
  ev.code_point = cp;
  ev.text = u16_text;
  ev.modifiers = mods;
  return ev;
}

// static
std::vector<NativeEvent> MahoMcpInputSynthesizer::BuildNativeEventsForUnit(
    const TypingKeyUnit& unit) {
  std::vector<NativeEvent> events;
  if (unit.code_point == U'\n' || unit.code_point == U'\r') {
    events.push_back(NativeEvent::MakeKeyDown(ui::VKEY_RETURN));
    events.push_back(NativeEvent::MakeKeyUp(ui::VKEY_RETURN));
  } else {
    events.push_back(NativeEvent::MakeKeyChar(unit.code_point, unit.utf16));
  }
  return events;
}

TestNativeInputDispatcher::TestNativeInputDispatcher() = default;
TestNativeInputDispatcher::~TestNativeInputDispatcher() = default;

NativeDispatchResult TestNativeInputDispatcher::Dispatch(
    base::span<const NativeEvent> events) {
  if (fail_all_) {
    NativeDispatchResult res;
    res.requested = events.size();
    res.inserted = 0;
    res.raw_os_error = simulated_error_code_;
    res.submission_known = true;
    res.error_message = "simulated dispatch failure";
    return res;
  }

  size_t to_insert = events.size();
  if (max_insert_count_.has_value() && *max_insert_count_ < to_insert) {
    to_insert = *max_insert_count_;
  }

  for (size_t i = 0; i < to_insert; ++i) {
    dispatched_events_.push_back(events[i]);
  }

  NativeDispatchResult res;
  res.requested = events.size();
  res.inserted = to_insert;
  res.raw_os_error = (to_insert < events.size()) ? simulated_error_code_ : 0;
  res.submission_known = true;
  return res;
}

NativeActionRequest::NativeActionRequest() = default;
NativeActionRequest::~NativeActionRequest() = default;
NativeActionRequest::NativeActionRequest(const NativeActionRequest&) = default;
NativeActionRequest& NativeActionRequest::operator=(
    const NativeActionRequest&) = default;
NativeActionRequest::NativeActionRequest(NativeActionRequest&&) noexcept =
    default;
NativeActionRequest& NativeActionRequest::operator=(
    NativeActionRequest&&) noexcept = default;

bool NativeActionRequest::Validate() const {
  if (frame_token.empty()) {
    return false;
  }
  if (lease_epoch == 0) {
    return false;
  }
  switch (type) {
    case ActionType::kClick: {
      if (!std::isfinite(click_point_css.x()) ||
          !std::isfinite(click_point_css.y())) {
        return false;
      }
      if (target_rect_css.width() <= 0.0f ||
          target_rect_css.height() <= 0.0f ||
          !std::isfinite(target_rect_css.x()) ||
          !std::isfinite(target_rect_css.y()) ||
          !std::isfinite(target_rect_css.width()) ||
          !std::isfinite(target_rect_css.height())) {
        return false;
      }
      if (!target_rect_css.Contains(click_point_css)) {
        return false;
      }
      return true;
    }
    case ActionType::kType:
      return !text.empty();
    case ActionType::kKey:
      return !key.empty();
  }
}

NativeActionResult::NativeActionResult() = default;
NativeActionResult::~NativeActionResult() = default;
NativeActionResult::NativeActionResult(const NativeActionResult&) = default;
NativeActionResult& NativeActionResult::operator=(
    const NativeActionResult&) = default;
NativeActionResult::NativeActionResult(NativeActionResult&&) noexcept = default;
NativeActionResult& NativeActionResult::operator=(
    NativeActionResult&&) noexcept = default;

NativeSecurityContext::NativeSecurityContext() = default;
NativeSecurityContext::~NativeSecurityContext() = default;
NativeSecurityContext::NativeSecurityContext(const NativeSecurityContext&) =
    default;
NativeSecurityContext& NativeSecurityContext::operator=(
    const NativeSecurityContext&) = default;
NativeSecurityContext::NativeSecurityContext(
    NativeSecurityContext&&) noexcept = default;
NativeSecurityContext& NativeSecurityContext::operator=(
    NativeSecurityContext&&) noexcept = default;

// static
std::vector<uint16_t> MahoMcpInputSynthesizer::CodePointToUtf16(
    char32_t code_point) {
  std::vector<uint16_t> units;
  if (code_point < 0x10000) {
    units.push_back(static_cast<uint16_t>(code_point));
  } else if (code_point <= 0x10FFFF) {
    const uint32_t offset = code_point - 0x10000;
    const uint16_t high = static_cast<uint16_t>(0xD800 + (offset >> 10));
    const uint16_t low = static_cast<uint16_t>(0xDC00 + (offset & 0x3FF));
    units.push_back(high);
    units.push_back(low);
  }
  return units;
}

uint16_t WindowsVkForKeyboardCode(ui::KeyboardCode key_code) {
  switch (key_code) {
    case ui::VKEY_RETURN: return 0x0D;  // VK_RETURN
    case ui::VKEY_TAB: return 0x09;     // VK_TAB
    case ui::VKEY_ESCAPE: return 0x1B;  // VK_ESCAPE
    case ui::VKEY_BACK: return 0x08;    // VK_BACK
    case ui::VKEY_DELETE: return 0x2E;  // VK_DELETE
    case ui::VKEY_UP: return 0x26;      // VK_UP
    case ui::VKEY_DOWN: return 0x28;    // VK_DOWN
    case ui::VKEY_LEFT: return 0x25;    // VK_LEFT
    case ui::VKEY_RIGHT: return 0x27;   // VK_RIGHT
    case ui::VKEY_HOME: return 0x24;    // VK_HOME
    case ui::VKEY_END: return 0x23;     // VK_END
    case ui::VKEY_PRIOR: return 0x21;   // VK_PRIOR
    case ui::VKEY_NEXT: return 0x22;    // VK_NEXT
    case ui::VKEY_SHIFT: return 0x10;   // VK_SHIFT
    case ui::VKEY_CONTROL: return 0x11; // VK_CONTROL
    case ui::VKEY_MENU: return 0x12;    // VK_MENU
    case ui::VKEY_COMMAND: return 0x5B; // VK_LWIN / VK_COMMAND
    case ui::VKEY_SPACE: return 0x20;   // VK_SPACE
    default: return static_cast<uint16_t>(key_code);
  }
}

uint16_t WindowsScanCodeForKeyboardCode(ui::KeyboardCode key_code) {
  switch (key_code) {
    case ui::VKEY_RETURN: return 0x1C;
    case ui::VKEY_TAB: return 0x0F;
    case ui::VKEY_ESCAPE: return 0x01;
    case ui::VKEY_BACK: return 0x0E;
    case ui::VKEY_DELETE: return 0x53;
    case ui::VKEY_UP: return 0x48;
    case ui::VKEY_DOWN: return 0x50;
    case ui::VKEY_LEFT: return 0x4B;
    case ui::VKEY_RIGHT: return 0x4D;
    case ui::VKEY_HOME: return 0x47;
    case ui::VKEY_END: return 0x4F;
    case ui::VKEY_PRIOR: return 0x49;
    case ui::VKEY_NEXT: return 0x51;
    case ui::VKEY_SHIFT: return 0x2A;
    case ui::VKEY_CONTROL: return 0x1D;
    case ui::VKEY_MENU: return 0x38;
    case ui::VKEY_COMMAND: return 0x5B;
    case ui::VKEY_SPACE: return 0x39;
    default: return 0;
  }
}

bool WindowsIsExtendedKey(ui::KeyboardCode key_code) {
  switch (key_code) {
    case ui::VKEY_UP:
    case ui::VKEY_DOWN:
    case ui::VKEY_LEFT:
    case ui::VKEY_RIGHT:
    case ui::VKEY_HOME:
    case ui::VKEY_END:
    case ui::VKEY_PRIOR:
    case ui::VKEY_NEXT:
    case ui::VKEY_DELETE:
    case ui::VKEY_COMMAND:
      return true;
    default:
      return false;
  }
}

std::vector<WinSimulatedInput> BuildWindowsInputsForNativeEvent(
    const NativeEvent& event) {
  constexpr uint32_t kWinExtendedKey = 0x0001;
  constexpr uint32_t kWinKeyUp = 0x0002;
  constexpr uint32_t kWinUnicode = 0x0004;

  constexpr uint32_t kWinMouseLeftDown = 0x0002;
  constexpr uint32_t kWinMouseLeftUp = 0x0004;

  std::vector<WinSimulatedInput> inputs;
  switch (event.type) {
    case NativeEvent::Type::kMouseMove: {
      WinSimulatedInput inp;
      inp.type = WinSimulatedInput::kMouse;
      inp.mouse_dx = event.point.win_normalized_x;
      inp.mouse_dy = event.point.win_normalized_y;
      inp.mouse_flags = event.point.win_flags;
      inputs.push_back(inp);
      break;
    }
    case NativeEvent::Type::kMouseDown: {
      WinSimulatedInput inp;
      inp.type = WinSimulatedInput::kMouse;
      inp.mouse_dx = event.point.win_normalized_x;
      inp.mouse_dy = event.point.win_normalized_y;
      inp.mouse_flags = event.point.win_flags | kWinMouseLeftDown;
      inputs.push_back(inp);
      break;
    }
    case NativeEvent::Type::kMouseUp: {
      WinSimulatedInput inp;
      inp.type = WinSimulatedInput::kMouse;
      inp.mouse_dx = event.point.win_normalized_x;
      inp.mouse_dy = event.point.win_normalized_y;
      inp.mouse_flags = event.point.win_flags | kWinMouseLeftUp;
      inputs.push_back(inp);
      break;
    }
    case NativeEvent::Type::kKeyDown: {
      WinSimulatedInput inp;
      inp.type = WinSimulatedInput::kKeyboard;
      inp.vk = WindowsVkForKeyboardCode(event.key_code);
      inp.scan = WindowsScanCodeForKeyboardCode(event.key_code);
      inp.key_flags = WindowsIsExtendedKey(event.key_code) ? kWinExtendedKey : 0;
      inputs.push_back(inp);
      break;
    }
    case NativeEvent::Type::kKeyUp: {
      WinSimulatedInput inp;
      inp.type = WinSimulatedInput::kKeyboard;
      inp.vk = WindowsVkForKeyboardCode(event.key_code);
      inp.scan = WindowsScanCodeForKeyboardCode(event.key_code);
      inp.key_flags = kWinKeyUp | (WindowsIsExtendedKey(event.key_code) ? kWinExtendedKey : 0);
      inputs.push_back(inp);
      break;
    }
    case NativeEvent::Type::kKeyChar: {
      std::vector<uint16_t> utf16_units;
      if (!event.text.empty()) {
        for (char16_t c : event.text) {
          utf16_units.push_back(static_cast<uint16_t>(c));
        }
      } else if (event.code_point > 0) {
        utf16_units = MahoMcpInputSynthesizer::CodePointToUtf16(event.code_point);
      }
      for (uint16_t u : utf16_units) {
        WinSimulatedInput down;
        down.type = WinSimulatedInput::kKeyboard;
        down.vk = 0;
        down.scan = u;
        down.unicode_char = u;
        down.key_flags = kWinUnicode;
        inputs.push_back(down);

        WinSimulatedInput up;
        up.type = WinSimulatedInput::kKeyboard;
        up.vk = 0;
        up.scan = u;
        up.unicode_char = u;
        up.key_flags = kWinUnicode | kWinKeyUp;
        inputs.push_back(up);
      }
      break;
    }
  }
  return inputs;
}

NativeInputOperation::NativeInputOperation(
    content::WebContents* wc,
    const VisualFrame& frame,
    const NativeActionRequest& request,
    NativeInputDispatcher* dispatcher,
    NativeSecurityContext context,
    base::OnceCallback<void(NativeActionResult)> callback)
    : web_contents_(wc ? wc->GetWeakPtr() : nullptr),
      frame_(frame),
      request_(request),
      dispatcher_(dispatcher),
      context_(std::move(context)),
      callback_(std::move(callback)) {}

NativeInputOperation::~NativeInputOperation() = default;

bool NativeInputOperation::IsContextPreserved() const {
  if (!context_.is_foreground_window || !context_.is_target_tab_active) {
    return false;
  }
  if (context_.is_occluded) {
    return false;
  }
  if (context_.foreground_check && !context_.foreground_check.Run()) {
    return false;
  }
  if (context_.focus_check && !context_.focus_check.Run()) {
    return false;
  }
  return true;
}

void NativeInputOperation::Start() {
  state_ = State::kMoving;

  if (!dispatcher_) {
#if BUILDFLAG(IS_MAC)
    dispatcher_ = GetPlatformNativeDispatcherMac();
#elif BUILDFLAG(IS_WIN)
    dispatcher_ = GetPlatformNativeDispatcherWin();
#else
    FinishWithError(-32011, "Native input unsupported on this platform");
    return;
#endif
  }

  // 1. Request argument validation
  if (!request_.Validate()) {
    FinishWithError(-32602, "Invalid native action request parameters");
    return;
  }

  // 2. Frame token and staleness validation
  if (frame_.token.empty() || frame_.is_stale) {
    FinishWithError(-32009, "Visual frame is empty or stale");
    return;
  }
  if (request_.document_epoch > 0 &&
      frame_.document_epoch != request_.document_epoch) {
    FinishWithError(-32009, "Frame document epoch mismatch");
    return;
  }

  // 3. Lease authorization: verify live lease epoch matches request
  if (context_.lease_epoch_lookup) {
    uint64_t live_epoch = context_.lease_epoch_lookup.Run(request_.tab_id);
    if (live_epoch == 0 || live_epoch != request_.lease_epoch) {
      FinishWithError(-32007, "Lease epoch mismatch or lease not held");
      return;
    }
  }

  // 4. Foreground / window / occlusion validation
  if (!IsContextPreserved()) {
    FinishWithError(-32011, "Target window not in foreground or occluded");
    return;
  }

  // 5. User modifier safety check: never consume unknown-user-held modifiers
  if (context_.user_held_modifiers || request_.user_modifiers_active) {
    FinishWithError(-32011,
                    "User-held modifiers detected; aborted to prevent corruption");
    return;
  }

  // Action branch
  switch (request_.type) {
    case NativeActionRequest::ActionType::kClick: {
      std::optional<NativePoint> pt =
          MahoMcpInputSynthesizer::ResolveNativePoint(frame_,
                                                     request_.click_point_css);
      if (pt) {
        target_point_ = *pt;
      } else {
        std::optional<gfx::PointF> dip =
            MahoMcpInputSynthesizer::CssToScreenDip(frame_,
                                                   request_.click_point_css);
        if (!dip) {
          FinishWithError(-32009,
                          "Failed to resolve CSS point to screen coordinates");
          return;
        }
        target_point_.screen_dip = *dip;
        target_point_.mac_cg_point = *dip;
      }

      // Dispatch initial mouse move to target
      NativeEvent move_ev = NativeEvent::MakeMouseMove(target_point_);
      NativeDispatchResult res = dispatcher_->Dispatch(
          base::span_from_ref(move_ev));
      events_dispatched_ += res.inserted;
      if (!res.ok()) {
        FinishWithError(-32011, "Failed to dispatch native mouse move");
        return;
      }

      state_ = State::kPrePressDwell;
      const int dwell_ms = request_.motion_profile.pre_click_dwell_ms.min_ms;
      if (dwell_ms > 0 && base::SequencedTaskRunner::HasCurrentDefault()) {
        base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
            FROM_HERE,
            base::BindOnce(&NativeInputOperation::DoPress, AsWeakPtr()),
            base::Milliseconds(dwell_ms));
      } else {
        DoPress();
      }
      break;
    }
    case NativeActionRequest::ActionType::kType:
      ExecuteTypeAction();
      break;
    case NativeActionRequest::ActionType::kKey:
      ExecuteKeyAction();
      break;
  }
}

void NativeInputOperation::DoPress() {
  if (cancelled_) {
    FinishCancelled(/*cleanup_required=*/false);
    return;
  }

  // Re-verify authorization right before press
  if (context_.lease_epoch_lookup) {
    uint64_t live_epoch = context_.lease_epoch_lookup.Run(request_.tab_id);
    if (live_epoch == 0 || live_epoch != request_.lease_epoch) {
      FinishCancelled(/*cleanup_required=*/false, -32007,
                      "Lease revoked prior to press");
      return;
    }
  }
  if (!IsContextPreserved()) {
    FinishCancelled(/*cleanup_required=*/false, -32011,
                    "Target context lost prior to press");
    return;
  }

  state_ = State::kPressed;
  mouse_pressed_ = true;

  NativeEvent down_ev = NativeEvent::MakeMouseDown(
      target_point_, NativeEvent::MouseButton::kLeft);
  NativeDispatchResult res = dispatcher_->Dispatch(base::span_from_ref(down_ev));
  events_dispatched_ += res.inserted;
  if (!res.ok()) {
    mouse_pressed_ = false;
    FinishWithError(-32011, "Failed to dispatch native mouse down");
    return;
  }

  state_ = State::kPostPressDwell;
  const int gap_ms = request_.motion_profile.press_release_ms.min_ms;
  if (gap_ms > 0 && base::SequencedTaskRunner::HasCurrentDefault()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&NativeInputOperation::DoRelease, AsWeakPtr()),
        base::Milliseconds(gap_ms));
  } else {
    DoRelease();
  }
}

void NativeInputOperation::DoRelease() {
  if (cancelled_ || !IsContextPreserved()) {
    HandleInterruptionDuringPress();
    return;
  }

  NativeEvent up_ev = NativeEvent::MakeMouseUp(
      target_point_, NativeEvent::MouseButton::kLeft);
  NativeDispatchResult res = dispatcher_->Dispatch(base::span_from_ref(up_ev));
  events_dispatched_ += res.inserted;
  mouse_pressed_ = false;
  cleanup_required_ = false;
  FinishSuccess();
}

void NativeInputOperation::ExecuteTypeAction() {
  std::vector<TypingKeyUnit> units =
      MahoMcpInputSynthesizer::Utf8ToTypingUnits(request_.text);

  if (cancelled_) {
    FinishCancelled(/*cleanup_required=*/false);
    return;
  }

  for (const auto& unit : units) {
    if (cancelled_ || !IsContextPreserved()) {
      HandleInterruptionDuringPress();
      return;
    }

    std::vector<NativeEvent> events =
        MahoMcpInputSynthesizer::BuildNativeEventsForUnit(unit);
    NativeDispatchResult res = dispatcher_->Dispatch(base::span(events));
    events_dispatched_ += res.inserted;
    if (!res.ok()) {
      if (unit.code_point == '\n' && res.inserted == 1) {
        held_keys_.push_back(ui::VKEY_RETURN);
        HandleInterruptionDuringPress();
      } else {
        FinishWithError(-32011, "Failed to dispatch native typing unit");
      }
      return;
    }
  }

  FinishSuccess();
}

void NativeInputOperation::ExecuteKeyAction() {
  if (cancelled_) {
    FinishCancelled(/*cleanup_required=*/false);
    return;
  }

  ui::KeyboardCode main_code =
      MahoMcpInputSynthesizer::KeyStringToKeyCode(request_.key);
  if (main_code == ui::VKEY_UNKNOWN) {
    FinishWithError(-32602, "Unknown key: " + request_.key);
    return;
  }

  std::vector<ui::KeyboardCode> mod_codes;
  for (const auto& mod : request_.modifiers) {
    if (mod == "shift") {
      mod_codes.push_back(ui::VKEY_SHIFT);
    } else if (mod == "ctrl" || mod == "control") {
      mod_codes.push_back(ui::VKEY_CONTROL);
    } else if (mod == "alt" || mod == "option") {
      mod_codes.push_back(ui::VKEY_MENU);
    } else if (mod == "meta" || mod == "command") {
      mod_codes.push_back(ui::VKEY_COMMAND);
    }
  }

  // Press modifiers
  for (ui::KeyboardCode mod_code : mod_codes) {
    if (cancelled_ || !IsContextPreserved()) {
      HandleInterruptionDuringPress();
      return;
    }
    held_keys_.push_back(mod_code);
    NativeEvent mod_down = NativeEvent::MakeKeyDown(mod_code);
    NativeDispatchResult res =
        dispatcher_->Dispatch(base::span_from_ref(mod_down));
    events_dispatched_ += res.inserted;
  }

  // Press main key
  if (cancelled_ || !IsContextPreserved()) {
    HandleInterruptionDuringPress();
    return;
  }
  held_keys_.push_back(main_code);
  NativeEvent main_down = NativeEvent::MakeKeyDown(main_code);
  NativeDispatchResult res =
      dispatcher_->Dispatch(base::span_from_ref(main_down));
  events_dispatched_ += res.inserted;

  // Release main key
  NativeEvent main_up = NativeEvent::MakeKeyUp(main_code);
  dispatcher_->Dispatch(base::span_from_ref(main_up));
  events_dispatched_++;
  held_keys_.pop_back();

  // Release modifiers in reverse order
  for (auto it = mod_codes.rbegin(); it != mod_codes.rend(); ++it) {
    NativeEvent mod_up = NativeEvent::MakeKeyUp(*it);
    dispatcher_->Dispatch(base::span_from_ref(mod_up));
    events_dispatched_++;
  }
  held_keys_.clear();

  FinishSuccess();
}

void NativeInputOperation::Cancel() {
  if (state_ == State::kCompleted || state_ == State::kFailed ||
      state_ == State::kCancelled) {
    return;
  }
  cancelled_ = true;
  if (mouse_pressed_ || !held_keys_.empty()) {
    HandleInterruptionDuringPress();
  } else {
    FinishCancelled(/*cleanup_required=*/false);
  }
}

void NativeInputOperation::HandleInterruptionDuringPress() {
  state_ = State::kCancelled;
  cancelled_ = true;

  const bool context_preserved = IsContextPreserved();
  if (context_preserved) {
    // Target context preserved: safely neutralize operation-owned presses!
    if (mouse_pressed_) {
      NativeEvent up_ev = NativeEvent::MakeMouseUp(
          target_point_, NativeEvent::MouseButton::kLeft);
      dispatcher_->Dispatch(base::span_from_ref(up_ev));
      events_dispatched_++;
      mouse_pressed_ = false;
    }
    for (auto it = held_keys_.rbegin(); it != held_keys_.rend(); ++it) {
      NativeEvent up_ev = NativeEvent::MakeKeyUp(*it);
      dispatcher_->Dispatch(base::span_from_ref(up_ev));
      events_dispatched_++;
    }
    held_keys_.clear();
    cleanup_required_ = false;
    FinishCancelled(/*cleanup_required=*/false, -32011,
                    "Operation cancelled; compensating release dispatched");
  } else {
    // Context changed or focus lost: NEVER blindly release into foreign target!
    cleanup_required_ = true;
    FinishCancelled(/*cleanup_required=*/true, -32011,
                    "Target context lost while pressed; cleanup_required, input blocked");
  }
}

void NativeInputOperation::FinishSuccess() {
  state_ = State::kCompleted;
  NativeActionResult result;
  result.success = true;
  result.cancelled = false;
  result.cleanup_required = false;
  result.events_dispatched = events_dispatched_;
  auto cb = std::move(callback_);
  if (cb) {
    std::move(cb).Run(std::move(result));
  }
  delete this;
}

void NativeInputOperation::FinishCancelled(bool cleanup_required,
                                           int error_code,
                                           const std::string& message) {
  state_ = State::kCancelled;
  cleanup_required_ = cleanup_required;
  NativeActionResult result;
  result.success = false;
  result.cancelled = true;
  result.cleanup_required = cleanup_required;
  result.error_code = error_code;
  result.error_message = message;
  result.events_dispatched = events_dispatched_;
  auto cb = std::move(callback_);
  if (cb) {
    std::move(cb).Run(std::move(result));
  }
  delete this;
}

void NativeInputOperation::FinishWithError(int error_code,
                                          const std::string& message) {
  state_ = State::kFailed;
  cleanup_required_ = false;
  NativeActionResult result;
  result.success = false;
  result.cancelled = false;
  result.cleanup_required = false;
  result.error_code = error_code;
  result.error_message = message;
  result.events_dispatched = events_dispatched_;
  auto cb = std::move(callback_);
  if (cb) {
    std::move(cb).Run(std::move(result));
  }
  delete this;
}

// static
base::WeakPtr<NativeInputOperation>
MahoMcpInputSynthesizer::DispatchNativeActionAsync(
    content::WebContents* wc,
    const VisualFrame& frame,
    const NativeActionRequest& request,
    NativeInputDispatcher* dispatcher,
    NativeSecurityContext context,
    base::OnceCallback<void(NativeActionResult)> callback) {
  auto op = std::make_unique<NativeInputOperation>(
      wc, frame, request, dispatcher, std::move(context), std::move(callback));
  base::WeakPtr<NativeInputOperation> weak_op = op->AsWeakPtr();
  NativeInputOperation* raw_op = op.release();
  raw_op->Start();
  return weak_op;
}

// static
base::WeakPtr<NativeInputOperation>
MahoMcpInputSynthesizer::DispatchNativeActionAsync(
    content::WebContents* wc,
    const VisualFrame& frame,
    const NativeActionRequest& request,
    NativeInputDispatcher* dispatcher,
    base::OnceCallback<void(NativeActionResult)> callback) {
  NativeSecurityContext default_context;
  default_context.tab_id = request.tab_id;
  default_context.expected_lease_epoch = request.lease_epoch;
  default_context.expected_document_epoch = request.document_epoch;
  return DispatchNativeActionAsync(wc, frame, request, dispatcher,
                                   std::move(default_context),
                                   std::move(callback));
}
#endif

}  // namespace maho
