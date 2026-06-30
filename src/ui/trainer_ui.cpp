/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 *
 * Minimal ImGui window scaffold. Renders a placeholder panel that reports the
 * configured backend and LM readiness. The XPLM glue (full-screen invisible
 * capture window + draw-phase callback + GL state save/restore) follows the
 * proven pattern from the ATC template's src/ui/atc_ui.cpp.
 */

#include "ui/trainer_ui.hpp"

#include "airports/airport_db.hpp"
#include "backends/manager.hpp"
#include "persistence/settings.hpp"
#include "preflight/flight_suggester.hpp"

#include <XPLMDisplay.h>
#include <XPLMGraphics.h>
#include <XPLMProcessing.h>
#include <XPLMUtilities.h>

#include <imgui.h>
#include <imgui_impl_opengl2.h>

#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <algorithm>
#include <string>
#include <vector>

namespace trainer_ui {

namespace {

// The XPLM window is full-screen and invisible (DecorationNone). It exists
// only to capture mouse/keyboard events and feed them to ImGui, which draws
// its own window on top.
XPLMWindowID window_id = nullptr;
bool visible = false;
bool window_pos_reset_pending_ = false;
float last_frame_time_ = 0.0f;

constexpr float kDefaultW = 420.0f;
constexpr float kDefaultH = 300.0f;

float get_xp_time() { return XPLMGetElapsedTime(); }

// ── Input glue ───────────────────────────────────────────────────

// Feed the cursor position to ImGui and report whether ImGui wants the mouse
// (cursor over an ImGui window). Otherwise the click passes through to X-Plane.
bool imgui_wants_mouse_at(XPLMWindowID wnd, int x, int y) {
  int left, top, right, bottom;
  XPLMGetWindowGeometry(wnd, &left, &top, &right, &bottom);
  ImGuiIO &io = ImGui::GetIO();
  io.AddMousePosEvent(static_cast<float>(x - left),
                      static_cast<float>(top - y));
  return io.WantCaptureMouse;
}

int wnd_mouse_cb(XPLMWindowID wnd, int x, int y, XPLMMouseStatus status, void *) {
  if (!imgui_wants_mouse_at(wnd, x, y))
    return 0;
  ImGuiIO &io = ImGui::GetIO();
  if (status == xplm_MouseDown)
    io.AddMouseButtonEvent(0, true);
  if (status == xplm_MouseUp)
    io.AddMouseButtonEvent(0, false);
  return 1;
}

int wnd_wheel_cb(XPLMWindowID wnd, int x, int y, int, int clicks, void *) {
  if (!imgui_wants_mouse_at(wnd, x, y))
    return 0;
  ImGui::GetIO().AddMouseWheelEvent(0.0f, static_cast<float>(clicks));
  return 1;
}

XPLMCursorStatus wnd_cursor_cb(XPLMWindowID, int, int, void *) {
  return xplm_CursorDefault;
}

void wnd_key_cb(XPLMWindowID, char, XPLMKeyFlags, char vkey, void *,
                int losing_focus) {
  if (losing_focus) {
    ImGui::GetIO().AddFocusEvent(false);
    return;
  }
  // Scaffold window has no text input; only handle Escape to close.
  if (vkey == XPLM_VK_ESCAPE) {
    visible = false;
    if (window_id) {
      XPLMSetWindowIsVisible(window_id, 0);
      XPLMTakeKeyboardFocus(nullptr);
    }
  }
}

void wnd_draw_cb(XPLMWindowID, void *) {
  // Nothing — rendering happens in the draw phase callback.
}

// ── Pre-flight dialog ────────────────────────────────────────────

// Assumed GA groundspeed for converting a max-duration limit to a distance.
constexpr double kAssumedGaGroundspeedKts = 110.0;

const char *facility_short(airports::FacilityType f) {
  switch (f) {
  case airports::FacilityType::TOWERED:
    return "Tower";
  case airports::FacilityType::AFIS:
    return "AFIS";
  case airports::FacilityType::UNCONTROLLED:
    return "Unctrl";
  case airports::FacilityType::UNKNOWN:
    return "?";
  }
  return "?";
}

void draw_main_window() {
  ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "VFR Trainer");
#ifdef XP_WELLYS_TRAINER_VERSION
  ImGui::SameLine();
  ImGui::TextDisabled("v%s", XP_WELLYS_TRAINER_VERSION);
#endif
  ImGui::Separator();

  if (!airports::airport_db::ready()) {
    ImGui::TextDisabled("Loading DACH airports from apt.dat...");
    return;
  }

  ImGui::Text("Pre-flight: %zu DACH airports", airports::airport_db::count());
  ImGui::Spacing();

  // ── Criteria (rule-based, no LLM) ──────────────────────────────
  static int country_idx = 0; // ANY, DE, CH, AT
  static int atc_idx = 0;     // ANY, T->T, T->AFIS, AFIS->AFIS, AFIS->T
  static int diff_idx = 0;    // ANY, EASY, MEDIUM, HARD
  static int limit_mode = 0;  // 0 = distance km, 1 = duration min
  static int distance_km = 150;
  static int duration_min = 60;
  static std::vector<preflight::Suggestion> results;
  static bool searched = false;

  static const char *country_labels[] = {"Any", "Germany", "Switzerland",
                                          "Austria"};
  static const char *atc_labels[] = {"Any", "Tower -> Tower", "Tower -> AFIS",
                                     "AFIS -> AFIS", "AFIS -> Tower"};
  static const char *diff_labels[] = {"Any", "Easy", "Medium", "Hard"};
  static const char *limit_labels[] = {"Max distance (km)",
                                       "Max duration (min)"};

  ImGui::Combo("Country", &country_idx, country_labels, 4);
  ImGui::Combo("ATC type", &atc_idx, atc_labels, 5);
  ImGui::Combo("Difficulty", &diff_idx, diff_labels, 4);
  ImGui::Combo("Limit by", &limit_mode, limit_labels, 2);
  if (limit_mode == 0)
    ImGui::SliderInt("##dist", &distance_km, 10, 400, "%d km");
  else
    ImGui::SliderInt("##dur", &duration_min, 10, 240, "%d min");

  if (ImGui::Button("Suggest flights")) {
    preflight::Criteria c;
    c.country = static_cast<preflight::Country>(country_idx);
    c.atc_type = static_cast<preflight::AtcType>(atc_idx);
    c.difficulty = static_cast<preflight::Difficulty>(diff_idx);
    if (limit_mode == 0)
      c.max_distance_km = static_cast<double>(distance_km);
    else
      c.max_distance_km = (static_cast<double>(duration_min) / 60.0) *
                          kAssumedGaGroundspeedKts * 1.852; // kt -> km/h
    results = preflight::suggest_flights(airports::airport_db::airports(), c);
    searched = true;
  }

  if (!searched)
    return;

  ImGui::Separator();
  if (results.empty()) {
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                       "No matching airports - relax your criteria.");
    return;
  }

  // The difficulty value is a provisional rule-based placeholder until the LLM
  // scoring (#5) lands. Make that unmistakable in the table.
  ImGui::TextDisabled(
      "Difficulty: ~N = provisional rule-based estimate (real LLM score #5)");

  ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                          ImGuiTableFlags_SizingFixedFit;
  if (ImGui::BeginTable("suggestions", 5, flags)) {
    ImGui::TableSetupColumn("Departure");
    ImGui::TableSetupColumn("Destination");
    ImGui::TableSetupColumn("km");
    ImGui::TableSetupColumn("ATC");
    ImGui::TableSetupColumn("Diff");
    ImGui::TableHeadersRow();
    for (const auto &s : results) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::Text("%s %s", s.dep_icao.c_str(), s.dep_name.c_str());
      ImGui::TableSetColumnIndex(1);
      ImGui::Text("%s %s", s.dest_icao.c_str(), s.dest_name.c_str());
      ImGui::TableSetColumnIndex(2);
      ImGui::Text("%.0f", s.distance_km);
      ImGui::TableSetColumnIndex(3);
      ImGui::Text("%s->%s", facility_short(s.dep_facility),
                  facility_short(s.dest_facility));
      ImGui::TableSetColumnIndex(4);
      if (s.difficulty_source == preflight::DifficultySource::PROVISIONAL_RULE)
        ImGui::TextDisabled("~%d", s.dest_difficulty);
      else
        ImGui::Text("%d", s.dest_difficulty);
    }
    ImGui::EndTable();
  }
}

// ── Draw phase callback (ImGui rendering) ────────────────────────

int draw_phase_cb(XPLMDrawingPhase, int, void *) {
  if (!visible)
    return 1;

  int gl, gt, gr, gb;
  XPLMGetScreenBoundsGlobal(&gl, &gt, &gr, &gb);
  int sw = gr - gl;
  int sh = gt - gb;
  if (sw <= 0 || sh <= 0)
    return 1;

  // Keep the capture window sized to full screen.
  if (window_id) {
    int wl, wt, wr, wb;
    XPLMGetWindowGeometry(window_id, &wl, &wt, &wr, &wb);
    if (wl != gl || wb != gb || wr != gr || wt != gt)
      XPLMSetWindowGeometry(window_id, gl, gt, gr, gb);
  }

  // Save GL state.
  GLint prev_viewport[4];
  glGetIntegerv(GL_VIEWPORT, prev_viewport);
  glPushAttrib(GL_TRANSFORM_BIT | GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT |
               GL_DEPTH_BUFFER_BIT | GL_SCISSOR_BIT | GL_TEXTURE_BIT);
  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_LIGHTING);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glViewport(0, 0, sw, sh);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0, sw, sh, 0, -1, 1); // top-left origin for ImGui
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  // ImGui frame setup.
  ImGuiIO &io = ImGui::GetIO();
  float now = get_xp_time();
  io.DeltaTime = std::max(now - last_frame_time_, 0.001f);
  last_frame_time_ = now;
  io.DisplaySize = ImVec2(static_cast<float>(sw), static_cast<float>(sh));

  int gmx, gmy;
  XPLMGetMouseLocationGlobal(&gmx, &gmy);
  io.AddMousePosEvent(static_cast<float>(gmx - gl),
                      static_cast<float>(gt - gmy));

  ImGui_ImplOpenGL2_NewFrame();
  ImGui::NewFrame();

  // Window position/size — from settings or centered.
  if (window_pos_reset_pending_) {
    ImGui::SetNextWindowPos(ImVec2((static_cast<float>(sw) - kDefaultW) * 0.5f,
                                   (static_cast<float>(sh) - kDefaultH) * 0.5f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kDefaultW, kDefaultH), ImGuiCond_Always);
    window_pos_reset_pending_ = false;
  } else {
    float sx = settings::window_x();
    float sy = settings::window_y();
    float sw_s = settings::window_w();
    float sh_s = settings::window_h();
    if (sx >= 0.0f && sy >= 0.0f) {
      ImGui::SetNextWindowPos(ImVec2(sx, sy), ImGuiCond_FirstUseEver);
    } else {
      ImGui::SetNextWindowPos(
          ImVec2((static_cast<float>(sw) - kDefaultW) * 0.5f,
                 (static_cast<float>(sh) - kDefaultH) * 0.5f),
          ImGuiCond_FirstUseEver);
    }
    ImGui::SetNextWindowSize(
        ImVec2(sw_s > 0.0f ? sw_s : kDefaultW, sh_s > 0.0f ? sh_s : kDefaultH),
        ImGuiCond_FirstUseEver);
  }
  ImGui::SetNextWindowSizeConstraints(ImVec2(320, 200), ImVec2(1920, 1080));

  bool open = true;
  if (ImGui::Begin("Welly's VFR Trainer", &open, ImGuiWindowFlags_NoCollapse)) {
    draw_main_window();

    // Persist geometry on change.
    ImVec2 pos = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetWindowSize();
    if (pos.x != settings::window_x() || pos.y != settings::window_y() ||
        size.x != settings::window_w() || size.y != settings::window_h()) {
      settings::set_window_geometry(pos.x, pos.y, size.x, size.y);
      settings::save();
    }
  }
  ImGui::End();

  if (!open) {
    visible = false;
    if (window_id) {
      XPLMSetWindowIsVisible(window_id, 0);
      XPLMTakeKeyboardFocus(nullptr);
    }
  }

  ImGui::Render();
  ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

  // Restore GL state.
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);
  glPopMatrix();
  glPopAttrib();
  glViewport(prev_viewport[0], prev_viewport[1], prev_viewport[2],
             prev_viewport[3]);

  return 1;
}

void ensure_capture_window() {
  if (window_id)
    return;
  int gl, gt, gr, gb;
  XPLMGetScreenBoundsGlobal(&gl, &gt, &gr, &gb);

  XPLMCreateWindow_t p{};
  p.structSize = sizeof(p);
  p.left = gl;
  p.bottom = gb;
  p.right = gr;
  p.top = gt;
  p.visible = 1;
  p.drawWindowFunc = wnd_draw_cb;
  p.handleMouseClickFunc = wnd_mouse_cb;
  p.handleKeyFunc = wnd_key_cb;
  p.handleCursorFunc = wnd_cursor_cb;
  p.handleMouseWheelFunc = wnd_wheel_cb;
  p.handleRightClickFunc = nullptr;
  p.refcon = nullptr;
  p.decorateAsFloatingWindow = xplm_WindowDecorationNone;
  p.layer = xplm_WindowLayerFloatingWindows;
  window_id = XPLMCreateWindowEx(&p);
}

} // namespace

// ── Public lifecycle ─────────────────────────────────────────────

void init() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ImGuiIO &io = ImGui::GetIO();
  static std::string ini_path = settings::get_data_dir() + "/imgui.ini";
  io.IniFilename = ini_path.c_str();
  io.LogFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

  ImGui::StyleColorsDark();
  auto &style = ImGui::GetStyle();
  style.WindowRounding = 6.0f;
  style.FrameRounding = 3.0f;
  style.WindowPadding = ImVec2(8, 6);

  ImGui_ImplOpenGL2_Init();
  last_frame_time_ = get_xp_time();

  XPLMRegisterDrawCallback(draw_phase_cb, xplm_Phase_Window, 1, nullptr);
}

void stop() {
  XPLMUnregisterDrawCallback(draw_phase_cb, xplm_Phase_Window, 1, nullptr);

  if (window_id) {
    XPLMDestroyWindow(window_id);
    window_id = nullptr;
  }
  ImGui_ImplOpenGL2_Shutdown();
  ImGui::DestroyContext();
}

void toggle() {
  visible = !visible;
  if (visible)
    ensure_capture_window();

  if (window_id) {
    XPLMSetWindowIsVisible(window_id, visible ? 1 : 0);
    if (visible)
      XPLMBringWindowToFront(window_id);
    else
      XPLMTakeKeyboardFocus(nullptr);
  }
}

void reset_window_position() {
  settings::reset_window_geometry();
  window_pos_reset_pending_ = true;
  if (!visible)
    toggle();
}

} // namespace trainer_ui
