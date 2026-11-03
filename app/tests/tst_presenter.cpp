#include "FileSource.h"
#include "History.h"
#include "Presenter.h"
#include "Recording.h"

#include <QTemporaryDir>
#include <QtTest>

#include <thread>

namespace {

constexpr qint64 ms = 1'000'000;

struct Shown
{
    qint64 wallNs;
    qint64 t;   // the sample's own time (original capture)
    int stream;
    quint64 seq;
};

// What the screen would show, recorded from the presenter's sinks.
struct Screen
{
    std::mutex m;
    std::vector<Shown> lidar, tracks;
    std::vector<quint64> log; // event log contents, as the model would hold them
    int resets = 0;
    std::vector<rec::CameraMark> marks;
    std::map<quint64, qint64> captureOf; // lidar seq*16+sensor -> original capture time

    Presenter::Sinks sinks()
    {
        return {
            [this](const qp::SensorFrame &f) {
                std::lock_guard g(m);
                lidar.push_back({qp::nowNs(), captureOf[f.seq * 16 + quint64(f.sensorId)], f.sensorId, f.seq});
            },
            [this](const qp::TrackSetPtr &s) {
                std::lock_guard g(m);
                tracks.push_back({qp::nowNs(), s->captureNs, 0, s->seq});
            },
            [this](const qp::EventBatchPtr &b, bool reset) {
                std::lock_guard g(m);
                if (reset) {
                    log.clear();
                    ++resets;
                }
                for (const qp::EventRecord &e : *b)
                    log.push_back(e.seq);
            },
            [this](const rec::CameraMark &mk, qint64, double, bool, qint64) {
                std::lock_guard g(m);
                marks.push_back(mk);
            },
        };
    }
};

// A small live world: 2 lidar sensors at 20 Hz, tracks at 10 Hz, one event per 10 ms.
struct World
{
    std::shared_ptr<History> history = std::make_shared<History>(qint64(1) << 30, History::Shape::Shared);
    std::shared_ptr<const qp::PointCloud> cloud = std::make_shared<qp::PointCloud>(qp::PointCloud{{1.f, 2.f, 3.f}});
    Screen *screen = nullptr;
    Presenter *presenter = nullptr;
    quint64 eventSeq = 0, lidarSeq[2] = {0, 0}, trackSeq = 0;
    int ticks = 0;

    // One 10 ms tick of ingest, with an optional lidar processing delay.
    void tick(qint64 lidarDelayNs = 0)
    {
        const qint64 now = qp::nowNs();
        auto ev = std::make_shared<qp::EventBatch>(1);
        (*ev)[0].seq = eventSeq++;
        (*ev)[0].captureNs = now;
        ingestEvents(ev);
        if (ticks % 5 == 0)
            for (int s = 0; s < 2; ++s) {
                qp::SensorFrame f{s, lidarSeq[s]++, now - lidarDelayNs, cloud};
                {
                    std::lock_guard g(screen->m);
                    screen->captureOf[f.seq * 16 + quint64(s)] = f.captureNs;
                }
                presenter->ingest([&] { history->addLidar(f); }, [&] { presenter_lidar(f); });
            }
        if (ticks % 10 == 0) {
            auto set = std::make_shared<qp::TrackSet>();
            set->seq = trackSeq++;
            set->captureNs = now;
            presenter->ingest([&] { history->addTracks(set); }, [&] { screenSinks.tracks(set); });
        }
        ++ticks;
    }
    void ingestEvents(const qp::EventBatchPtr &ev)
    {
        presenter->ingest([&] { history->addEvents(ev); }, [&] { screenSinks.events(ev, false); });
    }
    void presenter_lidar(const qp::SensorFrame &f) { screenSinks.lidar(f); }
    void run(int n, qint64 lidarDelayNs = 0)
    {
        for (int i = 0; i < n; ++i) {
            tick(lidarDelayNs);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    Presenter::Sinks screenSinks;
};

bool contiguous(const std::vector<quint64> &v)
{
    for (size_t i = 1; i < v.size(); ++i)
        if (v[i] != v[i - 1] + 1)
            return false;
    return true;
}

} // namespace

class TstPresenter : public QObject
{
    Q_OBJECT

private slots:
    void liveWithoutDelayPassesThrough()
    {
        Screen screen;
        Presenter p(screen.sinks());
        World w;
        w.screen = &screen;
        w.presenter = &p;
        w.screenSinks = screen.sinks();
        p.setSource(w.history, true);
        p.start();
        QTRY_VERIFY(p.passingThrough());
        w.run(50);
        std::lock_guard g(screen.m);
        QCOMPARE(screen.log.size(), size_t(50));
        QVERIFY(contiguous(screen.log));
        for (const Shown &s : screen.lidar)
            QVERIFY(s.wallNs - s.t < 2 * ms);
    }

    void liveDelayShowsEverySampleAtCaptureTimePlusDelay()
    {
        Screen screen;
        Presenter p(screen.sinks());
        World w;
        w.screen = &screen;
        w.presenter = &p;
        w.screenSinks = screen.sinks();
        p.setSource(w.history, true);
        p.setDelay(80 * ms);
        p.start();
        QTRY_VERIFY(!p.passingThrough() && p.delayNs() == 80 * ms);
        w.run(100, 30 * ms); // lidar arrives 30 ms after capture, inside the 80 ms delay
        QTest::qWait(150);
        std::lock_guard g(screen.m);
        QVERIFY(screen.lidar.size() >= 30);
        qint64 worstEarly = 0, worstLate = 0;
        for (const Shown &s : screen.lidar) {
            if (s.t == 0)
                continue; // the refresh at start
            const qint64 off = s.wallNs - (s.t + 80 * ms);
            worstEarly = std::min(worstEarly, off);
            worstLate = std::max(worstLate, off);
        }
        QVERIFY2(worstEarly > -1 * ms, qPrintable(QString::number(worstEarly / 1e6)));
        QVERIFY2(worstLate < 5 * ms, qPrintable(QString::number(worstLate / 1e6)));
        QVERIFY(contiguous(screen.log));
    }

    // Toggling the delay while data flows: the log never duplicates or skips an event.
    void switchingDelayKeepsTheLogExact()
    {
        Screen screen;
        Presenter p(screen.sinks());
        World w;
        w.screen = &screen;
        w.presenter = &p;
        w.screenSinks = screen.sinks();
        p.setSource(w.history, true);
        p.start();
        for (int round = 0; round < 10; ++round) {
            p.setDelay(round % 2 ? 0 : 40 * ms);
            w.run(13);
        }
        p.setDelay(0);
        QTRY_VERIFY(p.passingThrough());
        w.run(10);
        std::lock_guard g(screen.m);
        QVERIFY(contiguous(screen.log));
        QCOMPARE(screen.log.back(), w.eventSeq - 1);
        QVERIFY(screen.resets >= 5);
    }

    void seekShowsTheNewestSampleAtOrBeforeTheTarget()
    {
        Screen screen;
        Presenter p(screen.sinks());
        World w;
        w.screen = &screen;
        w.presenter = &p;
        w.screenSinks = screen.sinks();
        p.setSource(w.history, true);
        p.start();
        w.run(100);
        const qint64 target = w.history->startNs() + 500 * ms;
        {
            std::lock_guard g(screen.m);
            screen.lidar.clear();
            screen.tracks.clear();
        }
        p.seek(target);
        QTRY_COMPARE(p.mode(), Presenter::Mode::Paused);
        QTRY_COMPARE(p.playheadNs(), target);
        QTRY_VERIFY(screen.lidar.size() >= 2);
        std::lock_guard g(screen.m);
        for (const Shown &s : screen.lidar) {
            QVERIFY(s.t <= target);
            QVERIFY(s.t > target - 50 * ms); // 20 Hz: the newest is under 50 ms old
        }
        QCOMPARE(screen.tracks.size(), size_t(1));
        QVERIFY(screen.tracks[0].t <= target && screen.tracks[0].t > target - 100 * ms);
        QVERIFY(contiguous(screen.log));
        QVERIFY(screen.log.size() >= 45); // 5 s of backlog, but only ~0.5 s exists before the target
    }

    void stepMovesOneLidarFrame()
    {
        Screen screen;
        Presenter p(screen.sinks());
        World w;
        w.screen = &screen;
        w.presenter = &p;
        w.screenSinks = screen.sinks();
        p.setSource(w.history, true);
        p.start();
        w.run(60);
        p.seek(w.history->startNs() + 200 * ms);
        QTRY_COMPARE(p.mode(), Presenter::Mode::Paused);
        const qint64 before = p.playheadNs();
        p.step(+1);
        QTRY_VERIFY(p.playheadNs() != before);
        const qint64 next = w.history->nextAfter({rec::Type::Lidar, 0}, before).value();
        const qint64 next1 = w.history->nextAfter({rec::Type::Lidar, 1}, before).value();
        QCOMPARE(p.playheadNs(), std::min(next, next1));
        p.step(-1);
        QTRY_VERIFY(p.playheadNs() < std::min(next, next1));
    }

    void replayAtFourTimesDeliversTheWindowInOrder()
    {
        Screen screen;
        Presenter p(screen.sinks());
        World w;
        w.screen = &screen;
        w.presenter = &p;
        w.screenSinks = screen.sinks();
        p.setSource(w.history, true);
        p.start();
        w.run(120);
        p.pause();
        const qint64 from = w.history->startNs() + 100 * ms;
        p.seek(from);
        QTRY_COMPARE(p.playheadNs(), from);
        {
            std::lock_guard g(screen.m);
            screen.lidar.clear();
        }
        p.setRate(4);
        const qint64 wall0 = qp::nowNs();
        p.play();
        QTest::qWait(250); // 1 s of data
        const qint64 wall1 = qp::nowNs();
        p.pause();
        QTRY_COMPARE(p.mode(), Presenter::Mode::Paused);
        const double covered = double(p.playheadNs() - from) / double(wall1 - wall0);
        QVERIFY2(covered > 3.9 && covered < 4.1, qPrintable(QString::number(covered)));
        std::lock_guard g(screen.m);
        QVERIFY(contiguous(screen.log));
        quint64 last[2] = {0, 0};
        int frames = 0;
        for (const Shown &s : screen.lidar) {
            QVERIFY(s.seq >= last[s.stream]);
            last[s.stream] = s.seq;
            ++frames;
        }
        QVERIFY2(frames >= 30, qPrintable(QString::number(frames))); // 2 sensors × 20 Hz × ~1 s
    }

    void playingCatchesUpToLive()
    {
        Screen screen;
        Presenter p(screen.sinks());
        World w;
        w.screen = &screen;
        w.presenter = &p;
        w.screenSinks = screen.sinks();
        p.setSource(w.history, true);
        p.start();
        w.run(50);
        p.seek(w.history->startNs());
        QTRY_COMPARE(p.mode(), Presenter::Mode::Paused);
        p.setRate(8);
        p.play();
        std::thread producer([&] { w.run(30); });
        producer.join();
        QTRY_COMPARE(p.mode(), Presenter::Mode::Live);
        QTRY_VERIFY(p.passingThrough());
        w.run(10);
        std::lock_guard g(screen.m);
        QVERIFY(contiguous(screen.log));
        QCOMPARE(screen.log.back(), w.eventSeq - 1);
    }

    void fileReplayStopsAtTheEnd()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("p.qprec");
        rec::Writer writer(path, {});
        QVERIFY(writer.start(nullptr));
        const qint64 t0 = 1000 * ms;
        for (int i = 0; i < 20; ++i) {
            auto ev = std::make_shared<qp::EventBatch>(1);
            (*ev)[0].seq = quint64(i);
            (*ev)[0].captureNs = t0 + i * 10 * ms;
            writer.addEvents(ev);
            writer.addCameraMark({0, t0 + i * 10 * ms, i * 10'000});
        }
        writer.stop();
        auto file = std::make_shared<FileSource>();
        QVERIFY(file->open(path, nullptr));

        Screen screen;
        Presenter p(screen.sinks());
        p.setSource(file, false);
        p.start();
        QTRY_COMPARE(p.playheadNs(), t0);
        QCOMPARE(p.mode(), Presenter::Mode::Paused);
        p.play();
        QTRY_VERIFY(p.mode() == Presenter::Mode::Paused && p.playheadNs() == t0 + 190 * ms);
        std::lock_guard g(screen.m);
        QCOMPARE(screen.log.size(), size_t(20));
        QVERIFY(contiguous(screen.log));
        QVERIFY(screen.marks.size() >= 15); // cameras follow in replay
        p.goLive(); // a file can't go live
        QTest::qWait(50);
        QCOMPARE(p.mode(), Presenter::Mode::Paused);
    }

    void seeksCoalesce()
    {
        Screen screen;
        Presenter p(screen.sinks());
        World w;
        w.screen = &screen;
        w.presenter = &p;
        w.screenSinks = screen.sinks();
        p.setSource(w.history, true);
        p.start();
        w.run(50);
        const qint64 a = w.history->startNs(), b = w.history->endNs();
        qint64 last = 0;
        for (int i = 0; i < 2000; ++i)
            p.seek(last = a + (b - a) * (i % 97) / 97);
        QTRY_COMPARE(p.playheadNs(), last);
        QVERIFY(p.seeksDone() < p.seeksRequested());
        qInfo("seeks requested %llu, done %llu", p.seeksRequested(), p.seeksDone());
    }
};

QTEST_GUILESS_MAIN(TstPresenter)
#include "tst_presenter.moc"
