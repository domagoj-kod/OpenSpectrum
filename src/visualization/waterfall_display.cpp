// SPDX-License-Identifier: MIT

#include "waterfall_display.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace openspectrum
{

    WaterfallDisplay::WaterfallDisplay(size_t width, size_t height, size_t history_lines)
        : m_width(width),
          m_height(height),
          m_history_lines(std::min(history_lines, height)), // Cap at display height
          m_pixels(width * height * 4, 0),
          m_history_capacity(m_history_lines)
    {
        // Initialize history with empty lines
        for (size_t i = 0; i < m_history_capacity; ++i)
        {
            m_history.emplace_back(width, -120.0f); // Default to noise floor
        }
    }

    void WaterfallDisplay::set_db_range(float min_db, float max_db)
    {
        m_min_db = std::min(min_db, max_db);
        m_max_db = std::max(min_db, max_db);
        m_global_min = m_min_db;
        m_global_max = m_max_db;
    }

    void WaterfallDisplay::reset()
    {
        m_history.clear();
        for (size_t i = 0; i < m_history_capacity; ++i)
        {
            m_history.emplace_back(m_width, m_min_db);
        }
        m_global_min = m_min_db;
        m_global_max = m_max_db;
    }

    void WaterfallDisplay::add_spectrum_line(const std::vector<float> &db_values)
    {
        if (db_values.empty() || db_values.size() > m_width)
            return; // Invalid input

        // Resample if needed to match width
        std::vector<float> line(m_width, m_min_db);
        if (db_values.size() == m_width)
            line = db_values;
        else
        {
            // Downsample by averaging
            float scale = static_cast<float>(db_values.size()) / m_width;
            for (size_t i = 0; i < m_width; ++i)
            {
                size_t start = static_cast<size_t>(i * scale);
                size_t end = static_cast<size_t>((i + 1) * scale);
                end = std::min(end, db_values.size());

                float sum = 0.0f;
                for (size_t j = start; j < end; ++j)
                {
                    sum += db_values[j];
                }
                line[i] = sum / (end - start);
            }
        }

        // Add to history (circular buffer)
        m_history.push_back(std::move(line));
        if (m_history.size() > m_history_capacity)
        {
            m_history.pop_front();
        }

        // Update ranges if autoscale
        if (m_autoscale)
        {
            update_global_range();
        }

        render();
    }

    void WaterfallDisplay::update_global_range()
    {
        float new_min = m_min_db;
        float new_max = m_max_db;

        for (const auto &line : m_history)
        {
            if (!line.empty())
            {
                float line_min = *std::min_element(line.begin(), line.end());
                float line_max = *std::max_element(line.begin(), line.end());
                new_min = std::min(new_min, line_min);
                new_max = std::max(new_max, line_max);
            }
        }

        // Add margin
        float range = new_max - new_min;
        if (range > 0)
        {
            new_min -= range * 0.05f;
            new_max += range * 0.05f;
        }
        else
        {
            new_min -= 5.0f;
            new_max += 5.0f;
        }

        m_global_min = new_min;
        m_global_max = new_max;
    }

    void WaterfallDisplay::render()
    {
        if (m_history.empty())
        {
            std::fill(m_pixels.begin(), m_pixels.end(), 0);
            return;
        }

        const float db_range = m_global_max - m_global_min;
        if (db_range <= 0)
        {
            std::fill(m_pixels.begin(), m_pixels.end(), 0);
            return;
        }

        // Render history lines from bottom to top (oldest at bottom, newest at top)
        size_t y_offset = 0;
        const size_t line_height = std::max(1UL, m_height / m_history_capacity);

        for (auto it = m_history.begin(); it != m_history.end(); ++it, y_offset += line_height)
        {
            if (y_offset >= m_height)
                break;

            const auto &line = *it;
            size_t actual_line_height = (y_offset + line_height <= m_height) ? line_height : (m_height - y_offset);

            for (size_t y = 0; y < actual_line_height; ++y)
            {
                size_t pixel_row = y_offset + y;

                for (size_t x = 0; x < m_width && x < line.size(); ++x)
                {
                    float db = line[x];
                    auto color = m_palette.get_color(db, m_global_min, m_global_max);

                    size_t idx = (pixel_row * m_width + x) * 4;
                    if (idx + 4 <= m_pixels.size())
                    {
                        m_pixels[idx] = color.red;
                        m_pixels[idx + 1] = color.green;
                        m_pixels[idx + 2] = color.blue;
                        m_pixels[idx + 3] = color.alpha;
                    }
                }
            }
        }
    }

} // namespace openspectrum