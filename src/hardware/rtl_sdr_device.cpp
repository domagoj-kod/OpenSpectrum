// SPDX-License-Identifier: MIT

#include "rtl_sdr_device.h"
#include <stdexcept>

RtlSdrDevice::RtlSdrDevice(uint32_t index) : m_index(index) {}

bool RtlSdrDevice::open()
{
    if (m_dev)
        return true;

    if (rtlsdr_open(&m_dev, m_index) < 0)
        return false;

    // Recommended init order for librtlsdr
    rtlsdr_set_sample_rate(m_dev, m_sample_rate);
    rtlsdr_set_center_freq(m_dev, m_center_freq);
    rtlsdr_set_tuner_gain_mode(m_dev, 0); // 0 = auto gain (safer default)
    return true;
}

void RtlSdrDevice::close()
{
    if (m_dev)
    {
        rtlsdr_close(m_dev);
        m_dev = nullptr;
    }
}

RtlSdrDevice::~RtlSdrDevice() { close(); }

void RtlSdrDevice::reset_buffer()
{
    if (m_dev)
        rtlsdr_reset_buffer(m_dev);
}

void RtlSdrDevice::set_frequency(uint32_t freq_hz)
{
    m_center_freq = freq_hz;
    if (m_dev)
        rtlsdr_set_center_freq(m_dev, freq_hz);
}

void RtlSdrDevice::set_sample_rate(uint32_t rate_hz)
{
    m_sample_rate = rate_hz;
    if (m_dev)
        rtlsdr_set_sample_rate(m_dev, rate_hz); // calls the C library, not itself
}

void RtlSdrDevice::set_gain(float gain_db)
{
    m_gain = gain_db;
    if (m_dev)
    {
        rtlsdr_set_tuner_gain_mode(m_dev, 1); // switch to manual
        rtlsdr_set_tuner_gain(m_dev, static_cast<int>(gain_db * 10));
    }
}

std::vector<std::complex<float>> RtlSdrDevice::read_samples(size_t count)
{
    std::vector<uint8_t> buf(count * 2); // I + Q = 2 bytes per sample
    int n_read = 0;
    int ret = rtlsdr_read_sync(m_dev, buf.data(), static_cast<int>(buf.size()), &n_read);
    if (ret < 0 || n_read != static_cast<int>(buf.size()))
        throw std::runtime_error("RTL-SDR read failed or short read");

    std::vector<std::complex<float>> samples(count);
    for (size_t i = 0; i < count; ++i)
    {
        // RTL2832U outputs unsigned 8-bit; centre value 127/128 = DC
        samples[i] = std::complex<float>(
            (static_cast<float>(buf[i * 2]) - 127.5f) / 127.5f,
            (static_cast<float>(buf[i * 2 + 1]) - 127.5f) / 127.5f);
    }
    return samples;
}

void RtlSdrDevice::set_callback(SampleCallback cb)
{
    m_callback = std::move(cb);
}

// Stub implementations — to be fleshed out when async mode is needed
void RtlSdrDevice::start_streaming(size_t /*buffer_count*/) {}
void RtlSdrDevice::stop_streaming() {}

void RtlSdrDevice::static_callback(const uint8_t *buf, uint32_t len, void *ctx)
{
    static_cast<RtlSdrDevice *>(ctx)->process_callback(buf, len);
}

void RtlSdrDevice::process_callback(const uint8_t *buf, uint32_t len)
{
    if (!m_callback)
        return;
    size_t count = len / 2;
    std::vector<std::complex<float>> samples(count);
    for (size_t i = 0; i < count; ++i)
    {
        samples[i] = std::complex<float>(
            (static_cast<float>(buf[i * 2]) - 127.5f) / 127.5f,
            (static_cast<float>(buf[i * 2 + 1]) - 127.5f) / 127.5f);
    }
    m_callback(std::move(samples));
}