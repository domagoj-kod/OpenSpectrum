// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/plot_overlays.h"

#include <algorithm>
#include <cmath>

namespace openspectrum {

void Markers::apply(const std::vector<SdlRenderer::ClickEvent> &clicks,
                    bool clear, const PlotSpan &span, float plot_h) {
  if (clear) {
    m_hz.clear();
  }
  for (const auto &click : clicks) {
    if (click.x < 0.0F || click.x >= span.width || click.y < 0.0F ||
        click.y >= plot_h) {
      continue;
    }
    if (click.right) {
      double best = kRemoveRadiusPx;
      auto best_it = m_hz.end();
      for (auto it = m_hz.begin(); it != m_hz.end(); ++it) {
        const double mx = span.xrel(*it) * static_cast<double>(span.width);
        if (mx < 0.0 || mx >= static_cast<double>(span.width)) {
          continue;
        }
        const double d = std::abs(mx - static_cast<double>(click.x));
        if (d < best) {
          best = d;
          best_it = it;
        }
      }
      if (best_it != m_hz.end()) {
        m_hz.erase(best_it);
      }
    } else if (m_hz.size() < kMaxMarkers) {
      m_hz.push_back(span.freq_at(click.x));
    }
  }
}

std::vector<SdlRenderer::Marker>
Markers::draw_list(const PlotSpan &span,
                   const SpectrumDisplay &spectrum) const {
  const auto spec_h = static_cast<float>(spectrum.height());
  std::vector<SdlRenderer::Marker> out;
  out.reserve(m_hz.size());
  int idx = 1;
  for (const double f : m_hz) {
    SdlRenderer::Marker m;
    m.index = idx++;
    m.freq_hz = f;
    const double xrel = span.xrel(f);
    m.on_screen = (xrel >= 0.0 && xrel < 1.0);
    m.x = static_cast<float>(xrel * static_cast<double>(span.width));
    if (m.on_screen) {
      SpectrumDisplay::CursorSample s;
      if (spectrum.sample_at_x(m.x, span.width, spec_h, s)) {
        m.db = s.db;
      }
    }
    out.push_back(m);
  }
  return out;
}

SdlRenderer::CursorReadout
CursorTracker::update(bool active, float cx, float cy, const PlotSpan &span,
                      const SpectrumDisplay &spectrum,
                      const WaterfallDisplay &waterfall) {
  SdlRenderer::CursorReadout readout;
  const auto spec_h = static_cast<float>(spectrum.height());
  const auto wf_h = static_cast<float>(waterfall.height());
  const double freq_hz = span.freq_at(cx);

  bool in_spectrum = false;
  if (active && cx >= 0.0F && cx < span.width) {
    SpectrumDisplay::CursorSample sample;
    if (cy >= 0.0F && cy < spec_h &&
        spectrum.sample_at_x(cx, span.width, spec_h, sample)) {
      in_spectrum = true;
      if (!m_db_valid || sample.bin != m_db_bin) {
        m_db_ema = sample.db;
      } else {
        m_db_ema += kEmaAlpha * (static_cast<double>(sample.db) - m_db_ema);
      }
      m_db_bin = sample.bin;
      m_db_valid = true;

      readout.pane = SdlRenderer::CursorReadout::Pane::Spectrum;
      readout.x = cx;
      readout.dot_y = sample.dot_y;
      readout.freq_hz = freq_hz;
      readout.db = static_cast<float>(m_db_ema);
    } else if (cy >= spec_h && cy < spec_h + wf_h) {
      WaterfallDisplay::CursorTime when;
      if (waterfall.sample_at_y(cy - spec_h, when)) {
        readout.pane = SdlRenderer::CursorReadout::Pane::Waterfall;
        readout.x = cx;
        readout.dot_y = cy;
        readout.freq_hz = freq_hz;
        readout.seconds_ago = when.seconds_ago;
      }
    }
  }
  if (!in_spectrum) {
    m_db_valid = false;
  }
  return readout;
}

void Trigger::set_from_y(float y, float pane_h, float db_min, float db_max) {
  y = std::clamp(y, 0.0F, pane_h);
  if (y >= pane_h - kDisarmPx) {
    armed = false;
  } else if (db_max > db_min) {
    const float frac = y / pane_h;
    threshold_db = db_max - frac * (db_max - db_min);
    armed = true;
  }
}

float Trigger::line_y(float pane_h, float db_min, float db_max) const {
  if (db_max <= db_min) {
    return 0.0F;
  }
  const float frac = (db_max - threshold_db) / (db_max - db_min);
  return std::clamp(frac, 0.0F, 1.0F) * pane_h;
}

} // namespace openspectrum
