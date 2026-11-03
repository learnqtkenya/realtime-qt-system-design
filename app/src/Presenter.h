#pragma once

#include "Source.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

// The presentation clock. Every stream is shown at the playhead T:
//   live    T = now − delay
//   playing T advances at `rate` from where play started
//   paused  T stays
// Live with no delay passes samples straight through, as before the timeline existed.
// Runs on its own thread: commands never wait for the source, and a slow read delays the
// presenter, not the GUI.
class Presenter
{
public:
    enum class Mode { Live, Paused, Playing };

    struct Sinks
    {
        std::function<void(const qp::SensorFrame &)> lidar;
        std::function<void(const qp::TrackSetPtr &)> tracks;
        std::function<void(const qp::EventBatchPtr &, bool reset)> events;
        // T, the rate, whether playing, and the wall time that corresponds to T.
        std::function<void(const rec::CameraMark &, qint64 playheadNs, double rate, bool playing, qint64 wallNs)> camera;
    };

    explicit Presenter(Sinks sinks);
    ~Presenter();

    void start();
    void stop();

    // live: the source keeps growing at the present (the history), so the presenter can go live.
    void setSource(std::shared_ptr<Source> source, bool live);
    void goLive();
    void pause();
    void play();
    void setRate(double rate);
    void setDelay(qint64 delayNs);
    // The newest request wins: a drag that asks faster than seeks complete skips the stale ones.
    void seek(qint64 t);
    void step(int direction);

    // The ingest thread: `store` always runs; `deliver` runs too when passing straight through.
    // One lock covers both, so a switch in or out of pass-through never shows a sample twice or
    // drops one.
    template <typename Store, typename Deliver> void ingest(Store &&store, Deliver &&deliver)
    {
        {
            std::lock_guard g(m_passMutex);
            store();
            if (m_pass) {
                deliver();
                return;
            }
        }
        {
            std::lock_guard lk(m_mutex);
            m_dataArrived = true;
        }
        m_cv.notify_one();
    }

    Mode mode() const { return m_modeOut.load(); }
    qint64 playheadNs() const { return m_playheadOut.load(); }
    double rate() const { return m_rateOut.load(); }
    qint64 delayNs() const { return m_delayOut.load(); }
    bool passingThrough() const { return m_passOut.load(); }
    bool liveSource() const { return m_liveOut.load(); }
    quint64 seeksRequested() const { return m_seeksRequested.load(); }
    quint64 seeksDone() const { return m_seeksDone.load(); }

private:
    struct Commands
    {
        std::optional<Mode> mode;
        std::optional<double> rate;
        std::optional<qint64> delay;
        std::optional<qint64> seek;
        int step = 0;
        bool source = false;
        bool any() const { return mode || rate || delay || seek || step || source; }
    };
    using Key = std::pair<rec::Type, int>;

    void run();
    qint64 clock(qint64 now) const;
    qint64 dueWall(qint64 t) const;
    void anchor(qint64 t, qint64 now);
    void advance(qint64 T, qint64 now);
    void doSeek(qint64 target, qint64 now);
    void deliver(const Sample &s, qint64 T, qint64 stampNs, qint64 now, bool timed);
    void setPass(bool on, qint64 now);
    std::optional<qint64> stepTarget(qint64 T, int direction);
    void publish(qint64 T);

    Sinks m_sinks;
    std::thread m_thread;

    std::mutex m_mutex; // guards m_cmd, m_pendingSource, m_pendingLive, m_stop, m_dataArrived
    std::condition_variable m_cv;
    Commands m_cmd;
    std::shared_ptr<Source> m_pendingSource;
    bool m_pendingLive = false;
    bool m_stop = false;
    bool m_dataArrived = false;

    std::mutex m_passMutex;
    bool m_pass = false; // guarded by m_passMutex

    // presenter thread only
    std::shared_ptr<Source> m_src;
    bool m_live = false;
    Mode m_mode = Mode::Live;
    double m_rate = 1.0;
    qint64 m_delay = 0;
    qint64 m_pausedT = 0;
    qint64 m_anchorT = 0, m_anchorWall = 0;
    std::map<Key, qint64> m_cursors;

    std::atomic<Mode> m_modeOut{Mode::Live};
    std::atomic<qint64> m_playheadOut{0};
    std::atomic<double> m_rateOut{1.0};
    std::atomic<qint64> m_delayOut{0};
    std::atomic<bool> m_passOut{false};
    std::atomic<bool> m_liveOut{false};
    std::atomic<quint64> m_seeksRequested{0}, m_seeksDone{0};
};
