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
#include "airports/airport_score_cache.hpp"
#include "airports/airport_scorer.hpp"
#include "backends/loader.hpp"
#include "backends/manager.hpp"
#include "dependencies/dependencies.hpp"
#include "dependencies/xplm_plugin_probe.hpp"
#include "fms/fms_writer.hpp"
#include "persistence/settings.hpp"
#include "preflight/flight_suggester.hpp"
#include "ui/clipboard.hpp"

#include <XPLMDataAccess.h>
#include <XPLMDisplay.h>
#include <XPLMGraphics.h>
#include <XPLMNavigation.h>
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
#include <cstdio>
#include <cstring>
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

void wnd_key_cb(XPLMWindowID, char key, XPLMKeyFlags flags, char vkey, void *,
                int losing_focus) {
  if (losing_focus) {
    ImGui::GetIO().AddFocusEvent(false);
    return;
  }
  ImGuiIO &io = ImGui::GetIO();
  // Only consume keys when ImGui has an active text input (the Settings tab's
  // API-key field). Otherwise let X-Plane handle them (command bindings, etc.).
  if (!io.WantTextInput)
    return;
  bool is_down = (flags & xplm_DownFlag) != 0;
  bool is_up = (flags & xplm_UpFlag) != 0;
  // Map editing/navigation keys for both press and release so ImGui doesn't get
  // stuck with a "held" key (e.g. Backspace deleting forever).
  ImGuiKey ikey = ImGuiKey_None;
  if (vkey == XPLM_VK_BACK)
    ikey = ImGuiKey_Backspace;
  else if (vkey == XPLM_VK_DELETE)
    ikey = ImGuiKey_Delete;
  else if (vkey == XPLM_VK_RETURN)
    ikey = ImGuiKey_Enter;
  else if (vkey == XPLM_VK_LEFT)
    ikey = ImGuiKey_LeftArrow;
  else if (vkey == XPLM_VK_RIGHT)
    ikey = ImGuiKey_RightArrow;
  else if (vkey == XPLM_VK_HOME)
    ikey = ImGuiKey_Home;
  else if (vkey == XPLM_VK_END)
    ikey = ImGuiKey_End;
  else if (vkey == XPLM_VK_TAB)
    ikey = ImGuiKey_Tab;
  if (ikey != ImGuiKey_None) {
    if (is_down)
      io.AddKeyEvent(ikey, true);
    if (is_up)
      io.AddKeyEvent(ikey, false);
  }
  if (is_down && key >= 32 && key < 127)
    io.AddInputCharacter(static_cast<unsigned>(key));
  if (is_down && vkey == XPLM_VK_ESCAPE) {
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

// Short country code from an ICAO prefix (display only). Empty for prefixes
// outside the DACH set so a border crossing is easy to spot in the table.
const char *country_code(const std::string &icao) {
  if (icao.size() < 2)
    return "";
  const std::string p = icao.substr(0, 2);
  if (p == "ED" || p == "ET")
    return "DE";
  if (p == "LS")
    return "CH";
  if (p == "LO")
    return "AT";
  return "";
}

const airports::Airport *find_airport(const std::vector<airports::Airport> &v,
                                      const std::string &icao) {
  for (const auto &a : v)
    if (a.icao == icao)
      return &a;
  return nullptr;
}

// Resolve elevations for the departure/destination from the airport DB and
// inject a minimal dep -> dest plan into the FMS. Returned status is surfaced
// in the UI (success or a clear error — never a silent fail).
fms::InjectResult
load_selected_into_fms(const std::vector<airports::Airport> &apts,
                       const std::string &dep_icao,
                       const std::string &dest_icao) {
  const airports::Airport *dep = find_airport(apts, dep_icao);
  const airports::Airport *dest = find_airport(apts, dest_icao);
  if (!dep)
    return {false, "Departure " + dep_icao + " not in airport database"};
  if (!dest)
    return {false, "Destination " + dest_icao + " not in airport database"};
  return fms::inject_direct_plan(dep_icao, static_cast<int>(dep->elevation_ft),
                                 dest_icao,
                                 static_cast<int>(dest->elevation_ft));
}

// Dependency status (ATC plugin + xp_pilot), re-probed at most ~1/s since
// XPLMFindPluginBySignature is not free. Shared by the Settings and Flight tabs.
const std::vector<deps::DependencyState> &cached_dependency_states() {
  static deps::XplmPluginProbe probe;
  static std::vector<deps::DependencyState> states;
  static float last = -1.0f;
  const float now = get_xp_time();
  if (last < 0.0f || now - last > 1.0f) {
    last = now;
    states = deps::evaluate(probe);
  }
  return states;
}

// Difficulty bucket name + colour, so a bare score (e.g. 4) is readable as a
// band (Easy 1-3 / Medium 4-6 / Hard 7-10) matching the search criterion.
const char *difficulty_label(int score) {
  switch (preflight::difficulty_bucket(score)) {
  case preflight::Difficulty::EASY:
    return "Easy";
  case preflight::Difficulty::MEDIUM:
    return "Medium";
  case preflight::Difficulty::HARD:
    return "Hard";
  default:
    return "";
  }
}

ImVec4 difficulty_color(int score) {
  switch (preflight::difficulty_bucket(score)) {
  case preflight::Difficulty::EASY:
    return ImVec4(0.4f, 1.0f, 0.4f, 1.0f); // green
  case preflight::Difficulty::MEDIUM:
    return ImVec4(1.0f, 0.85f, 0.3f, 1.0f); // yellow
  case preflight::Difficulty::HARD:
    return ImVec4(1.0f, 0.5f, 0.4f, 1.0f); // red
  default:
    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
  }
}

// Current aircraft position from the sim datarefs. The refs are looked up once
// and cached. Returns false when they are unavailable (no aircraft loaded yet),
// leaving lat/lon untouched.
bool current_aircraft_position(double &lat, double &lon) {
  static XPLMDataRef lat_ref =
      XPLMFindDataRef("sim/flightmodel/position/latitude");
  static XPLMDataRef lon_ref =
      XPLMFindDataRef("sim/flightmodel/position/longitude");
  if (lat_ref == nullptr || lon_ref == nullptr)
    return false;
  lat = XPLMGetDatad(lat_ref);
  lon = XPLMGetDatad(lon_ref);
  return true;
}

// Resolve the airport at / nearest to the given position via X-Plane's nav
// database. This is authoritative — unlike our apt.dat-derived DACH list, which
// can omit a field (e.g. EDNY) when its frequency data is sparse and would then
// snap the departure to the wrong airport across a border (e.g. LSZR). Fills
// icao/name and the airport's reference coordinates; returns false when the nav
// DB has no airport (or is not loaded).
bool resolve_current_airport(double cur_lat, double cur_lon, std::string &icao,
                             std::string &name, double &apt_lat,
                             double &apt_lon) {
  float flat = static_cast<float>(cur_lat);
  float flon = static_cast<float>(cur_lon);
  XPLMNavRef ref =
      XPLMFindNavAid(nullptr, nullptr, &flat, &flon, nullptr, xplm_Nav_Airport);
  if (ref == XPLM_NAV_NOT_FOUND)
    return false;
  char id_buf[32] = {};
  char name_buf[256] = {};
  float alat = 0.0f, alon = 0.0f;
  XPLMGetNavAidInfo(ref, nullptr, &alat, &alon, nullptr, nullptr, nullptr,
                    id_buf, name_buf, nullptr);
  icao = id_buf;
  name = name_buf;
  apt_lat = alat;
  apt_lon = alon;
  return true;
}

void draw_flight_tab() {
  if (!airports::airport_db::ready()) {
    ImGui::TextDisabled("Loading DACH airports from apt.dat...");
    return;
  }

  const ImVec4 warn(1.0f, 0.6f, 0.2f, 1.0f);

  // ── Departure: live "From:" line ───────────────────────────────
  // Resolve the current airport from the nav DB, refreshed ~1/s (XPLMFindNavAid
  // is not free) so the line tracks the aircraft as it taxis between fields.
  static double last_resolve = -1.0;
  static bool dep_found = false;
  static std::string dep_icao, dep_name;
  static double anchor_lat = 0.0, anchor_lon = 0.0;

  const float now = get_xp_time();
  if (last_resolve < 0.0 || now - last_resolve > 1.0) {
    last_resolve = now;
    double cur_lat = 0.0, cur_lon = 0.0;
    dep_found = current_aircraft_position(cur_lat, cur_lon) &&
                resolve_current_airport(cur_lat, cur_lon, dep_icao, dep_name,
                                        anchor_lat, anchor_lon);
  }

  if (dep_found) {
    const char *cc = country_code(dep_icao);
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "From: %s %s%s%s%s",
                       dep_icao.c_str(), dep_name.c_str(), cc[0] ? " (" : "", cc,
                       cc[0] ? ")" : "");
  } else {
    ImGui::TextColored(warn, "From: position unavailable - load a flight first.");
  }

  ImGui::Separator();

  // Reporting/scoring gate: airport search + FMS work standalone, but the
  // post-flight evaluation needs both producer plugins. Surface it; don't block.
  if (!deps::all_ready(cached_dependency_states())) {
    ImGui::TextColored(warn, "Post-flight reporting disabled - needs xp_pilot + "
                             "ATC plugin (see Settings).");
    ImGui::Separator();
  }

  // ── Destination criteria (rule-based, no LLM) ──────────────────
  ImGui::TextDisabled("To:");
  static int dest_idx = 0;   // ANY, TOWER, AFIS (preflight::DestFacility)
  static int diff_idx = 0;   // ANY, EASY, MEDIUM, HARD
  static int limit_mode = 0; // 0 = distance km, 1 = duration min
  static int distance_km = 80;
  static int duration_min = 60;
  static std::vector<preflight::Suggestion> results;
  static preflight::Criteria searched_criteria; // frozen at last search
  static std::string searched_dep_icao; // departure at last search (for crossing)
  static bool searched_dep_ok = false;
  static bool searched = false;
  static int selected_row = -1;        // chosen suggestion for the FMS button
  static bool pending_confirm = false; // open the replace-plan modal next frame
  static fms::InjectResult last_result; // last FMS-load outcome, shown inline

  static const char *dest_labels[] = {"Any", "Tower", "AFIS"};
  static const char *diff_labels[] = {"Any", "Easy", "Medium", "Hard"};
  static const char *limit_labels[] = {"Max distance (km)",
                                       "Max duration (min)"};

  ImGui::Combo("Destination", &dest_idx, dest_labels, 3);
  ImGui::Combo("Difficulty", &diff_idx, diff_labels, 4);
  ImGui::Combo("Limit by", &limit_mode, limit_labels, 2);
  if (limit_mode == 0)
    ImGui::SliderInt("##dist", &distance_km, 10, 400, "%d km");
  else
    ImGui::SliderInt("##dur", &duration_min, 10, 240, "%d min");

  if (ImGui::Button("Suggest flights")) {
    preflight::Criteria c;
    c.dep_lat = anchor_lat;
    c.dep_lon = anchor_lon;
    c.dest_facility = static_cast<preflight::DestFacility>(dest_idx);
    c.difficulty = static_cast<preflight::Difficulty>(diff_idx);
    if (limit_mode == 0)
      c.max_distance_km = static_cast<double>(distance_km);
    else
      c.max_distance_km = (static_cast<double>(duration_min) / 60.0) *
                          kAssumedGaGroundspeedKts * 1.852; // kt -> km/h

    searched_criteria = c;
    searched_dep_icao = dep_found ? dep_icao : std::string();
    searched_dep_ok = dep_found;
    searched = true;
    selected_row = -1; // a fresh result set invalidates the old selection
    last_result = {};
  }

  if (!searched)
    return;

  // Recompute every frame with the frozen criteria so difficulty updates live
  // as the background LLM fills the cache without re-click. Enqueue ALL in-range
  // candidates (not just the displayed results) so the difficulty filter can be
  // applied to real scores; enqueue + the cache-backed ScoreFn are idempotent.
  const auto &apts = airports::airport_db::airports();
  if (searched_dep_ok) {
    for (const auto &c : preflight::candidates_in_range(apts, searched_criteria))
      airports::scorer::enqueue_if_missing(*c.apt);
    results = preflight::suggest_flights(apts, searched_criteria,
                                         airports::scorer::score_of);
  } else {
    results.clear();
  }

  ImGui::Separator();
  if (!searched_dep_ok) {
    ImGui::TextColored(warn, "No airport at your position - load a flight first.");
    return;
  }

  const bool filtering = searched_criteria.difficulty != preflight::Difficulty::ANY;
  if (!backends::lm_ready()) {
    ImGui::TextColored(warn,
                       "No API key set - difficulty scores unavailable (Settings tab).");
  } else if (airports::scorer::busy()) {
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f),
                       "Scoring %zu airports in background...",
                       airports::scorer::pending_count());
  }

  if (results.empty()) {
    if (filtering && backends::lm_ready() && airports::scorer::busy())
      ImGui::TextDisabled("Waiting for scores - matches appear as they are rated.");
    else
      ImGui::TextColored(warn, "No destinations within your criteria - relax them.");
    return;
  }

  ImGui::TextDisabled("Difficulty: ~N = provisional estimate, plain = LLM score");

  const char *dep_cc = country_code(searched_dep_icao);

  ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                          ImGuiTableFlags_SizingFixedFit;
  if (ImGui::BeginTable("suggestions", 6, flags)) {
    ImGui::TableSetupColumn("Destination");
    ImGui::TableSetupColumn("km");
    ImGui::TableSetupColumn("Fac");
    ImGui::TableSetupColumn("Ctry");
    ImGui::TableSetupColumn("Diff");
    // "Why" takes the remaining width and wraps, so the full rationale is shown
    // (widen the window for more room).
    ImGui::TableSetupColumn("Why", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    for (int row = 0; row < static_cast<int>(results.size()); ++row) {
      const auto &s = results[row];
      // Resolve the cached entry once: its rationale feeds the "Why" column.
      const airports::Airport *a = find_airport(apts, s.dest_icao);
      const airports::score_cache::Entry *entry =
          a ? airports::score_cache::lookup_entry(
                  s.dest_icao, airports::scorer::kPromptVersion,
                  airports::scorer::input_sig(*a))
            : nullptr;

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      // Whole row is selectable; the chosen destination drives the FMS button.
      char row_id[16];
      std::snprintf(row_id, sizeof(row_id), "##r%d", row);
      if (ImGui::Selectable(row_id, selected_row == row,
                            ImGuiSelectableFlags_SpanAllColumns))
        selected_row = row;
      ImGui::SameLine();
      ImGui::Text("%s %s", s.dest_icao.c_str(), s.dest_name.c_str());
      ImGui::TableSetColumnIndex(1);
      ImGui::Text("%.0f", s.distance_km);
      ImGui::TableSetColumnIndex(2);
      ImGui::Text("%s", facility_short(s.dest_facility));
      ImGui::TableSetColumnIndex(3);
      // Highlight a border crossing: destination country differs from departure.
      const char *dest_cc = country_code(s.dest_icao);
      const bool crossing =
          dest_cc[0] && dep_cc[0] && std::strcmp(dest_cc, dep_cc) != 0;
      if (crossing)
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", dest_cc);
      else
        ImGui::Text("%s", dest_cc);
      ImGui::TableSetColumnIndex(4);
      if (s.difficulty_source == preflight::DifficultySource::PROVISIONAL_RULE)
        ImGui::TextDisabled("~%d %s", s.dest_difficulty,
                            difficulty_label(s.dest_difficulty));
      else
        ImGui::TextColored(difficulty_color(s.dest_difficulty), "%d %s",
                           s.dest_difficulty,
                           difficulty_label(s.dest_difficulty));
      ImGui::TableSetColumnIndex(5);
      // "Why": full LLM rationale, wrapped to the column width. Empty until scored.
      if (entry && !entry->rationale.empty())
        ImGui::TextWrapped("%s", entry->rationale.c_str());
      else
        ImGui::TextDisabled("-");
    }
    ImGui::EndTable();
  }

  // ── Load selected destination into the FMS (optional) ──────────
  // Live re-scoring can shrink `results` under us; drop a stale selection.
  if (selected_row >= static_cast<int>(results.size()))
    selected_row = -1;

  ImGui::Separator();
  if (selected_row < 0) {
    ImGui::TextDisabled("Select a destination row to load it into the FMS.");
  } else {
    const auto &sel = results[selected_row];
    if (ImGui::Button("Load into FMS")) {
      // Confirm before clobbering an existing flight plan; inject directly when
      // the plan is empty.
      if (fms::primary_entry_count() > 0)
        pending_confirm = true;
      else
        last_result =
            load_selected_into_fms(apts, searched_dep_icao, sel.dest_icao);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(%s -> %s)", searched_dep_icao.c_str(),
                        sel.dest_icao.c_str());

    if (pending_confirm) {
      ImGui::OpenPopup("Replace flight plan?");
      pending_confirm = false;
    }
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Replace flight plan?", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("A flight plan is already loaded.\nReplace it with %s -> %s?",
                  searched_dep_icao.c_str(), sel.dest_icao.c_str());
      ImGui::Separator();
      if (ImGui::Button("Replace", ImVec2(120, 0))) {
        last_result =
            load_selected_into_fms(apts, searched_dep_icao, sel.dest_icao);
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel", ImVec2(120, 0)))
        ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
  }

  if (!last_result.message.empty()) {
    if (last_result.ok)
      ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s",
                         last_result.message.c_str());
    else
      ImGui::TextColored(warn, "%s", last_result.message.c_str());
  }
}

// ── Settings tab (LLM provider config) ───────────────────────────

// Renders the API-key controls + LM-model picker for one cloud provider. The
// settings layer keeps OpenAI and Mistral keys in separate Keychain entries
// and exposes parallel getters/setters, so the panel is parametrised by plain
// function pointers rather than duplicated per provider.
void draw_key_panel(bool (*key_saved)(), bool (*key_save)(const std::string &),
                    void (*key_delete)(), std::string (*model_get)(),
                    void (*model_set)(const std::string &),
                    const char *const *models, int model_count, char *buf,
                    size_t buf_size, float &fb_timer, char *fb_msg,
                    size_t fb_size) {
  if (key_saved())
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
                       "API key saved (Keychain)");
  else
    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "No API key configured");

  ImGui::InputText("API key", buf, buf_size, ImGuiInputTextFlags_Password);

  // Cmd+V into a password InputText is unreliable in the X-Plane ImGui context
  // (the sim grabs key events first) and ImGui::GetClipboardText() only sees
  // ImGui's internal buffer here — read NSPasteboard directly instead.
  if (ImGui::Button("Paste")) {
    std::string clip = ui::clipboard::read_system_text();
    if (!clip.empty()) {
      std::strncpy(buf, clip.c_str(), buf_size - 1);
      buf[buf_size - 1] = '\0';
      std::snprintf(fb_msg, fb_size, "Pasted %zu characters",
                    std::strlen(buf));
    } else {
      std::snprintf(fb_msg, fb_size, "Clipboard is empty");
    }
    fb_timer = 3.0f;
  }
  ImGui::SameLine();
  if (ImGui::Button("Save key")) {
    if (buf[0] != '\0' && key_save(buf)) {
      std::snprintf(fb_msg, fb_size, "API key saved");
      // The key now lives in the Keychain only — wipe the in-memory copy.
      std::memset(buf, 0, buf_size);
      backends::loader::stop();
      backends::loader::start();
    } else {
      std::snprintf(fb_msg, fb_size, "Save failed (empty key or Keychain error)");
    }
    fb_timer = 3.0f;
  }
  ImGui::SameLine();
  if (ImGui::Button("Delete key")) {
    key_delete();
    std::memset(buf, 0, buf_size);
    std::snprintf(fb_msg, fb_size, "API key deleted");
    fb_timer = 3.0f;
    backends::loader::stop();
    backends::loader::start();
  }
  if (fb_timer > 0.0f) {
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "%s", fb_msg);
    fb_timer -= ImGui::GetIO().DeltaTime;
  }

  // LM model picker. The current value is always offered even if it is not one
  // of the hard-coded presets, so a manually-edited settings.json is preserved.
  std::vector<std::string> opts(models, models + model_count);
  std::string cur = model_get();
  if (std::find(opts.begin(), opts.end(), cur) == opts.end())
    opts.insert(opts.begin(), cur);
  int sel = 0;
  for (size_t i = 0; i < opts.size(); ++i) {
    if (opts[i] == cur) {
      sel = static_cast<int>(i);
      break;
    }
  }
  std::vector<const char *> labels;
  labels.reserve(opts.size());
  for (const auto &s : opts)
    labels.push_back(s.c_str());
  if (ImGui::Combo("LM model", &sel, labels.data(),
                   static_cast<int>(labels.size()))) {
    model_set(opts[static_cast<size_t>(sel)]);
    settings::save();
  }
}

void draw_settings_tab() {
  // Cloud-only: provider + API key + LM model. No STT/TTS/voice or local-model
  // download (those belong to the ATC plugin, not the trainer — see CONCEPT.md).
  static const char *backend_keys[] = {"openai", "mistral"};
  static const char *backend_labels[] = {"OpenAI Cloud", "Mistral Cloud"};
  constexpr int kBackendCount = 2;

  static const char *openai_models[] = {"gpt-4o-mini", "gpt-4o", "gpt-4.1-mini",
                                        "gpt-4.1"};
  static const char *mistral_models[] = {"mistral-large-latest",
                                         "mistral-small-latest"};

  static char openai_buf[256] = {};
  static float openai_fb_timer = 0.0f;
  static char openai_fb_msg[128] = {};
  static char mistral_buf[256] = {};
  static float mistral_fb_timer = 0.0f;
  static char mistral_fb_msg[128] = {};

  ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "AI Backend");

  int backend_sel = 0;
  {
    std::string bm = settings::backend_mode();
    for (int i = 0; i < kBackendCount; ++i) {
      if (bm == backend_keys[i]) {
        backend_sel = i;
        break;
      }
    }
  }
  if (ImGui::Combo("Provider", &backend_sel, backend_labels, kBackendCount)) {
    settings::set_backend_mode(backend_keys[backend_sel]);
    settings::save();
    // Re-run the loader so the newly selected provider's key/model is picked up.
    backends::loader::stop();
    backends::loader::start();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Cloud LLM for airport difficulty scoring (#5) and "
                      "post-flight review (#6).");

  ImGui::Spacing();
  ImGui::Indent();
  if (std::string(backend_keys[backend_sel]) == "openai") {
    draw_key_panel(&settings::api_key_saved, &settings::save_api_key,
                   &settings::delete_api_key, &settings::openai_lm_model,
                   &settings::set_openai_lm_model, openai_models, 4, openai_buf,
                   sizeof(openai_buf), openai_fb_timer, openai_fb_msg,
                   sizeof(openai_fb_msg));
  } else {
    draw_key_panel(&settings::mistral_api_key_saved,
                   &settings::save_mistral_api_key,
                   &settings::delete_mistral_api_key,
                   &settings::mistral_lm_model, &settings::set_mistral_lm_model,
                   mistral_models, 2, mistral_buf, sizeof(mistral_buf),
                   mistral_fb_timer, mistral_fb_msg, sizeof(mistral_fb_msg));
  }
  ImGui::Unindent();

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Dependencies");
  ImGui::TextDisabled("Required for post-flight reporting & session scoring "
                      "(#6/#7). Airport search and FMS work without them.");
  ImGui::Spacing();
  const ImVec4 dep_ok(0.4f, 1.0f, 0.4f, 1.0f);
  const ImVec4 dep_bad(1.0f, 0.6f, 0.2f, 1.0f);
  for (const auto &d : cached_dependency_states()) {
    const bool ready = d.installed && d.enabled;
    ImGui::TextColored(ready ? dep_ok : dep_bad, "[%s] %s",
                       ready ? "OK" : "X", d.name.c_str());
    ImGui::Indent();
    if (!d.installed)
      ImGui::TextColored(dep_bad, "not installed");
    else if (!d.enabled)
      ImGui::TextColored(dep_bad, "installed but disabled");
    ImGui::TextDisabled("%s", d.output_path.c_str());
    ImGui::Unindent();
  }
}

void draw_main_window() {
  ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "VFR Trainer");
#ifdef XP_WELLYS_TRAINER_VERSION
  ImGui::SameLine();
  ImGui::TextDisabled("v%s", XP_WELLYS_TRAINER_VERSION);
#endif
  ImGui::Separator();

  if (ImGui::BeginTabBar("trainer_tabs")) {
    if (ImGui::BeginTabItem("Flight")) {
      draw_flight_tab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Settings")) {
      draw_settings_tab();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
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

  // Dynamic XPLM keyboard focus. The capture window only receives key events
  // while it holds focus, but toggle() never grabs it (so command bindings keep
  // firing). Mirror ImGui's per-frame WantTextInput onto the XPLM focus state so
  // the Settings-tab API-key field works, then release focus the moment no text
  // widget is active.
  if (window_id && visible) {
    bool want_text = ImGui::GetIO().WantTextInput;
    bool have_focus = XPLMHasKeyboardFocus(window_id) != 0;
    if (want_text && !have_focus)
      XPLMTakeKeyboardFocus(window_id);
    else if (!want_text && have_focus)
      XPLMTakeKeyboardFocus(nullptr);
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
