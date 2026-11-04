// The viewer's process-plugin protocol (API 1.x) in standard C++, no Qt. See sdk/python/qpwire.py
// for the format. Unknown records are skipped; item fields beyond the stride are ignored.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace qpwire {

constexpr uint16_t kTracks = 2, kOverlay = 6, kSelection = 7;

struct Track
{
    uint32_t id;
    float x, y, heading, speed, length, width;
    uint8_t cls;
};

class Overlay
{
public:
    void color(uint32_t argb) { m_rgba = argb; }
    void line(float x1, float y1, float x2, float y2)
    {
        put(x1);
        put(y1);
        put(x2);
        put(y2);
        put(m_rgba);
        ++m_count;
    }
    void text(float x, float y, const std::string &s)
    {
        m_labels.push_back({x, y, m_rgba, s.substr(0, 256)});
    }
    std::vector<char> encode() const
    {
        std::vector<char> out;
        append(out, uint32_t(m_count));
        append(out, uint16_t(20));
        out.insert(out.end(), m_segments.begin(), m_segments.end());
        append(out, uint32_t(m_labels.size()));
        for (const auto &l : m_labels) {
            append(out, uint16_t(14 + l.text.size()));
            append(out, l.x);
            append(out, l.y);
            append(out, l.rgba);
            append(out, uint16_t(l.text.size()));
            out.insert(out.end(), l.text.begin(), l.text.end());
        }
        return out;
    }

private:
    struct Label
    {
        float x, y;
        uint32_t rgba;
        std::string text;
    };
    template <typename T> void put(T v) { append(m_segments, v); }
    template <typename T> static void append(std::vector<char> &out, T v)
    {
        const char *p = reinterpret_cast<const char *>(&v);
        out.insert(out.end(), p, p + sizeof v);
    }
    std::vector<char> m_segments;
    size_t m_count = 0;
    std::vector<Label> m_labels;
    uint32_t m_rgba = 0xFFFFFFFF;
};

struct State
{
    int selected = -1;
};

namespace detail {
inline bool read(void *p, size_t n)
{
    return std::fread(p, 1, n, stdin) == n;
}
template <typename T> T load(const char *p)
{
    T v;
    std::memcpy(&v, p, sizeof v);
    return v;
}
} // namespace detail

// Calls update for every tracks record until the viewer goes away.
inline int run(const std::function<void(const std::vector<Track> &, Overlay &, const State &)> &update,
               const std::string &name)
{
    char hello[16];
    if (!detail::read(hello, 16) || std::memcmp(hello, "QPSTREAM", 8) != 0 || detail::load<uint16_t>(hello + 8) != 1)
        return 1;
    std::vector<char> buf(detail::load<uint32_t>(hello + 12));
    if (!detail::read(buf.data(), buf.size()))
        return 1;
    const std::string info = "{\"name\":\"" + name + "\",\"api\":\"1.0\"}";
    const uint16_t major = 1, minor = 0;
    const auto infoSize = uint32_t(info.size());
    std::fwrite("QPSTREAM", 1, 8, stdout);
    std::fwrite(&major, 2, 1, stdout);
    std::fwrite(&minor, 2, 1, stdout);
    std::fwrite(&infoSize, 4, 1, stdout);
    std::fwrite(info.data(), 1, info.size(), stdout);
    std::fflush(stdout);

    State state;
    std::vector<Track> tracks;
    char header[20];
    while (detail::read(header, 20)) {
        const auto type = detail::load<uint16_t>(header);
        const auto flags = detail::load<uint16_t>(header + 6);
        const auto t = detail::load<int64_t>(header + 8);
        buf.resize(detail::load<uint32_t>(header + 16));
        if (!detail::read(buf.data(), buf.size()))
            break;
        if (type == kSelection && buf.size() >= 4) {
            state.selected = detail::load<int32_t>(buf.data());
        } else if (type == kTracks && flags == 0 && buf.size() >= 14) {
            const auto n = detail::load<uint32_t>(buf.data() + 8);
            const auto stride = detail::load<uint16_t>(buf.data() + 12);
            if (stride < 29 || buf.size() < 14 + size_t(n) * stride)
                break;
            tracks.resize(n);
            for (uint32_t i = 0; i < n; ++i) {
                const char *p = buf.data() + 14 + size_t(i) * stride;
                Track &tr = tracks[i];
                tr.id = detail::load<uint32_t>(p);
                tr.x = detail::load<float>(p + 4);
                tr.y = detail::load<float>(p + 8);
                tr.heading = detail::load<float>(p + 12);
                tr.speed = detail::load<float>(p + 16);
                tr.length = detail::load<float>(p + 20);
                tr.width = detail::load<float>(p + 24);
                tr.cls = uint8_t(p[28]);
            }
            Overlay overlay;
            update(tracks, overlay, state);
            const std::vector<char> payload = overlay.encode();
            const uint16_t h[4] = {kOverlay, 1, 0, 0};
            const auto size = uint32_t(payload.size());
            std::fwrite(h, 2, 4, stdout);
            std::fwrite(&t, 8, 1, stdout);
            std::fwrite(&size, 4, 1, stdout);
            std::fwrite(payload.data(), 1, payload.size(), stdout);
            std::fflush(stdout);
        }
    }
    return 0;
}

} // namespace qpwire
