/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#include "postflight/atc_log.hpp"

#include <json.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>

using json = nlohmann::json;

namespace postflight {

AtcLog parse_atc_log(const std::string &json_text) {
  json doc;
  try {
    doc = json::parse(json_text);
  } catch (const std::exception &ex) {
    throw std::runtime_error(std::string("ATC log JSON parse error: ") +
                             ex.what());
  }
  if (!doc.is_object())
    throw std::runtime_error("ATC log: root is not an object");

  AtcLog log;
  log.version = doc.value("version", 0);
  if (log.version != kAtcLogVersion)
    throw std::runtime_error(
        "ATC log: unsupported version " + std::to_string(log.version) +
        " (need " + std::to_string(kAtcLogVersion) +
        " with UTC-epoch fields for correlation)");

  const auto flight = doc.find("flight");
  if (flight == doc.end() || !flight->is_object())
    throw std::runtime_error("ATC log: missing 'flight' object");
  if (!flight->contains("started_at_epoch"))
    throw std::runtime_error("ATC log: missing flight.started_at_epoch");

  log.flight.started_at = flight->value("started_at", std::string());
  log.flight.started_at_epoch = flight->value("started_at_epoch", std::int64_t{0});
  log.flight.departure_airport = flight->value("departure_airport", std::string());
  // destination_airport may be JSON null — value() with a default returns the
  // default for a present-but-null key.
  if (auto d = flight->find("destination_airport");
      d != flight->end() && d->is_string())
    log.flight.destination_airport = d->get<std::string>();
  log.flight.pilot_callsign = flight->value("pilot_callsign", std::string());
  log.flight.missing_initial_call = flight->value("missing_initial_call", false);

  const auto txs = doc.find("transmissions");
  if (txs == doc.end() || !txs->is_array())
    throw std::runtime_error("ATC log: missing 'transmissions' array");

  log.transmissions.reserve(txs->size());
  for (const auto &t : *txs) {
    if (!t.is_object())
      continue;
    if (!t.contains("ts"))
      throw std::runtime_error("ATC log: transmission missing 'ts' epoch field");
    Transmission tx;
    tx.ts = t.value("ts", std::int64_t{0});
    tx.time = t.value("time", std::string());
    tx.transcript = t.value("transcript", std::string());
    tx.atc_response = t.value("atc_response", std::string());
    tx.intent = t.value("intent", std::string());
    tx.confidence = t.value("confidence", 0.0);
    tx.flight_phase = t.value("flight_phase", std::string());
    tx.outcome = t.value("outcome", std::string());
    if (auto rb = t.find("readback_missing_elements");
        rb != t.end() && rb->is_array())
      tx.readback_issue = !rb->empty();
    if (auto fl = t.find("failure_locus"); fl != t.end() && fl->is_string())
      tx.failure_locus = fl->get<std::string>();
    log.transmissions.push_back(std::move(tx));
  }

  return log;
}

AtcLog load_atc_log(const std::string &path) {
  std::ifstream in(path);
  if (!in.good())
    throw std::runtime_error("ATC log: cannot open " + path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return parse_atc_log(ss.str());
}

} // namespace postflight
