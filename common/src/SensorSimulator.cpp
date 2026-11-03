#include "qp/SensorSimulator.h"

#include <QTimer>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace qp {

void registerSimTypes()
{
    static const bool once = [] {
        qRegisterMetaType<qp::SensorFrame>("qp::SensorFrame");
        qRegisterMetaType<qp::TrackSetPtr>("qp::TrackSetPtr");
        qRegisterMetaType<qp::EventBatchPtr>("qp::EventBatchPtr");
        return true;
    }();
    Q_UNUSED(once)
}

namespace {

constexpr int kPoolVariants = 4;
constexpr int kBeams = 64;
constexpr float kSensorHeight = 1.8f;
constexpr float kArena = 50.0f; // half-width, metres

struct Rng
{
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next()
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    float uniform() { return float(next() >> 8) * (1.0f / 16777216.0f); }
    float range(float lo, float hi) { return lo + (hi - lo) * uniform(); }
    float noise(float amp) { return (uniform() + uniform() + uniform() - 1.5f) * (amp * 0.667f); }
};

struct Pillar
{
    float x, y, r;
};

std::vector<Pillar> makePillars(uint32_t seed)
{
    Rng rng(seed * 7919u + 13u);
    std::vector<Pillar> out;
    out.reserve(64);
    for (int i = 0; i < 64; ++i) {
        Pillar p;
        do {
            p.x = rng.range(-45.f, 45.f);
            p.y = rng.range(-45.f, 45.f);
        } while (std::hypot(p.x, p.y) < 6.0f);
        p.r = rng.range(0.4f, 1.3f);
        out.push_back(p);
    }
    return out;
}

constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;

} // namespace

std::shared_ptr<const PointCloud>
SensorSimulator::makeCloud(const Workload &w, int sensorId, int variant)
{
    auto cloud = std::make_shared<PointCloud>();
    const int n = std::max(0, w.pointsPerFrame);
    cloud->xyz.resize(size_t(n) * 3);

    static constexpr float mountX[4] = {1.0f, 1.0f, -1.0f, -1.0f};
    static constexpr float mountY[4] = {0.8f, -0.8f, 0.8f, -0.8f};
    const float mx = mountX[sensorId & 3];
    const float my = mountY[sensorId & 3];

    const std::vector<Pillar> pillars = makePillars(w.seed);
    Rng rng(w.seed * 2654435761u + uint32_t(sensorId) * 40503u + uint32_t(variant) * 9973u + 1u);

    const int steps = std::max(1, n / kBeams);
    const float azStep = kTwoPi / float(steps);
    const float azPhase = float(variant) * azStep / float(kPoolVariants);
    constexpr float pitchMin = -24.8f * std::numbers::pi_v<float> / 180.0f;
    constexpr float pitchMax = -2.0f * std::numbers::pi_v<float> / 180.0f;

    float *out = cloud->xyz.data();
    for (int i = 0; i < n; ++i) {
        const uint32_t kind = rng.next() & 0xFF;
        float x, y, z;
        if (kind < 192) { // ~75% ground
            const int beam = i % kBeams;
            const int step = i / kBeams;
            const float pitch = pitchMin + (pitchMax - pitchMin) * float(beam) / float(kBeams - 1);
            const float r = kSensorHeight / std::tan(-pitch) + rng.noise(0.05f);
            const float az = float(step) * azStep + azPhase;
            x = mx + r * std::cos(az);
            y = my + r * std::sin(az);
            z = -kSensorHeight + rng.noise(0.04f);
        } else if (kind < 243) { // ~20% objects
            const Pillar &p = pillars[rng.next() & 63u];
            const float a = rng.range(0.f, kTwoPi);
            x = p.x + p.r * std::cos(a) + rng.noise(0.03f);
            y = p.y + p.r * std::sin(a) + rng.noise(0.03f);
            z = rng.range(-kSensorHeight, 0.0f);
        } else { // ~5% outliers
            x = rng.range(-kArena, kArena);
            y = rng.range(-kArena, kArena);
            z = rng.range(-kSensorHeight, 3.0f);
        }
        out[3 * i + 0] = x;
        out[3 * i + 1] = y;
        out[3 * i + 2] = z;
    }
    return cloud;
}

class SensorSimulator::Worker : public QObject
{
    Q_OBJECT
public:
    Worker(SensorSimulator *owner, const Workload &w) : m_owner(owner), m_w(w), m_rng(w.seed + 99u) {}

public slots:
    void init()
    {
        m_pool.resize(size_t(m_w.sensors));
        for (int s = 0; s < m_w.sensors; ++s)
            for (int v = 0; v < kPoolVariants; ++v)
                m_pool[size_t(s)].push_back(SensorSimulator::makeCloud(m_w, s, v));
        m_lidarSeq.assign(size_t(m_w.sensors), 0);

        initTracks();

        const int lidarMs = std::max(1, int(std::lround(1000.0 / m_w.lidarRateHz)));
        for (int s = 0; s < m_w.sensors; ++s) {
            auto *t = new QTimer(this);
            t->setTimerType(Qt::PreciseTimer);
            t->setInterval(lidarMs);
            connect(t, &QTimer::timeout, this, [this, s] { emitLidar(s); });
            const int offset = lidarMs * s / std::max(1, m_w.sensors);
            QTimer::singleShot(offset, Qt::PreciseTimer, this, [t] { t->start(); });
        }

        m_trackTimer = new QTimer(this);
        m_trackTimer->setTimerType(Qt::PreciseTimer);
        m_trackTimer->setInterval(std::max(1, int(std::lround(1000.0 / m_w.trackRateHz))));
        connect(m_trackTimer, &QTimer::timeout, this, &Worker::emitTracks);
        m_trackTimer->start();

        m_eventTimer = new QTimer(this);
        m_eventTimer->setTimerType(Qt::PreciseTimer);
        m_eventTimer->setInterval(kEventBatchMs);
        connect(m_eventTimer, &QTimer::timeout, this, &Worker::emitEvents);
        m_eventTimer->start();
    }

private:
    static constexpr int kEventBatchMs = 50;

    void initTracks()
    {
        m_tracks.resize(size_t(std::max(0, m_w.objects)));
        m_vel.resize(m_tracks.size());
        for (size_t i = 0; i < m_tracks.size(); ++i) {
            Track &t = m_tracks[i];
            t.id = quint32(1000 + i);
            t.cls = quint8(m_rng.next() & 3);
            t.x = m_rng.range(-kArena, kArena);
            t.y = m_rng.range(-kArena, kArena);
            t.heading = m_rng.range(0.f, kTwoPi);
            t.speed = t.cls == 3 ? m_rng.range(0.5f, 1.8f) : m_rng.range(2.f, 14.f);
            t.length = t.cls == 1 ? 9.0f : (t.cls == 0 ? 4.5f : (t.cls == 2 ? 1.8f : 0.6f));
            t.width = t.cls == 1 ? 2.5f : (t.cls == 0 ? 1.9f : (t.cls == 2 ? 0.6f : 0.6f));
        }
    }

    void emitLidar(int sensor)
    {
        SensorFrame f;
        f.sensorId = sensor;
        f.seq = m_lidarSeq[size_t(sensor)]++;
        f.captureNs = nowNs();
        f.cloud = m_pool[size_t(sensor)][m_w.still ? 0 : f.seq % kPoolVariants];
        m_owner->m_lidarEmitted.fetch_add(1, std::memory_order_relaxed);
        emit m_owner->lidarFrame(f);
    }

    void emitTracks()
    {
        const float dt = m_w.still ? 0.0f : float(1.0 / m_w.trackRateHz);
        for (Track &t : m_tracks) {
            if (!m_w.still)
                t.heading += m_rng.noise(0.08f);
            t.x += std::cos(t.heading) * t.speed * dt;
            t.y += std::sin(t.heading) * t.speed * dt;
            if (std::abs(t.x) > kArena || std::abs(t.y) > kArena)
                t.heading += std::numbers::pi_v<float>;
            t.x = std::clamp(t.x, -kArena, kArena);
            t.y = std::clamp(t.y, -kArena, kArena);
        }
        auto set = std::make_shared<TrackSet>();
        set->seq = m_trackSeq++;
        set->captureNs = nowNs();
        set->tracks = m_tracks;
        m_owner->m_trackEmitted.fetch_add(1, std::memory_order_relaxed);
        emit m_owner->trackSet(TrackSetPtr(std::move(set)));
    }

    void emitEvents()
    {
        m_eventCarry += m_w.eventsPerSecond * (kEventBatchMs / 1000.0);
        const int n = int(m_eventCarry);
        if (n <= 0)
            return;
        m_eventCarry -= n;

        static const char *kSources[] = {"lidar0", "lidar1", "lidar2", "lidar3",
                                         "tracker", "fusion", "system"};
        static const char *kInfo[] = {"frame received", "track confirmed", "calibration ok",
                                      "clock synchronised", "buffer swapped"};
        static const char *kWarn[] = {"frame arrived late", "low point density",
                                      "track association ambiguous"};
        static const char *kErr[] = {"packet checksum mismatch", "sensor timeout"};

        auto batch = std::make_shared<EventBatch>();
        batch->reserve(size_t(n));
        const int64_t now = nowNs();
        for (int i = 0; i < n; ++i) {
            EventRecord e;
            e.seq = m_eventSeq++;
            e.captureNs = now;
            const uint32_t r = m_rng.next() % 100;
            e.severity = r < 80 ? 0 : (r < 95 ? 1 : 2);
            e.source = QLatin1String(kSources[m_rng.next() % 7]);
            const char *msg = e.severity == 0   ? kInfo[m_rng.next() % 5]
                              : e.severity == 1 ? kWarn[m_rng.next() % 3]
                                                : kErr[m_rng.next() % 2];
            e.text = QLatin1String(msg);
            batch->push_back(std::move(e));
        }
        m_owner->m_eventsEmitted.fetch_add(quint64(n), std::memory_order_relaxed);
        emit m_owner->eventBatch(EventBatchPtr(std::move(batch)));
    }

    SensorSimulator *m_owner;
    Workload m_w;
    Rng m_rng;
    std::vector<std::vector<std::shared_ptr<const PointCloud>>> m_pool;
    std::vector<quint64> m_lidarSeq;
    std::vector<Track> m_tracks;
    std::vector<float> m_vel;
    QTimer *m_trackTimer = nullptr;
    QTimer *m_eventTimer = nullptr;
    quint64 m_trackSeq = 0;
    quint64 m_eventSeq = 0;
    double m_eventCarry = 0;
};

SensorSimulator::SensorSimulator(const Workload &workload, QObject *parent)
    : QObject(parent), m_workload(workload)
{
    registerSimTypes();
    m_thread.setObjectName(QStringLiteral("SensorSimulator"));
    m_worker = new Worker(this, m_workload);
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::started, m_worker, &Worker::init);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
}

SensorSimulator::~SensorSimulator()
{
    stop();
}

void SensorSimulator::start()
{
    if (!m_thread.isRunning())
        m_thread.start();
}

void SensorSimulator::stop()
{
    if (m_thread.isRunning()) {
        m_thread.quit();
        m_thread.wait();
    }
}

} // namespace qp

#include "SensorSimulator.moc"
