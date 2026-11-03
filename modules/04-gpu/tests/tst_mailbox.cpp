#include "LidarMailbox.h"

#include <QtTest>

#include <atomic>
#include <set>
#include <thread>

namespace {

qp::SensorFrame frame(int sensor, quint64 seq)
{
    qp::SensorFrame f;
    f.sensorId = sensor;
    f.seq = seq;
    f.captureNs = qp::nowNs();
    return f;
}

} // namespace

class TstMailbox : public QObject
{
    Q_OBJECT
private slots:
    void emptyUntilSubmitted()
    {
        LidarMailbox m(2);
        QCOMPARE(m.latest(0).serial, quint64(0));
        QVERIFY(!m.latest(1).frame.cloud);
    }

    void latestWins()
    {
        LidarMailbox m(1);
        for (quint64 s = 1; s <= 5; ++s)
            m.submit(frame(0, s));
        QCOMPARE(m.latest(0).frame.seq, quint64(5));
    }

    void serialsIncreaseAcrossSensors()
    {
        LidarMailbox m(2);
        m.submit(frame(0, 1));
        m.submit(frame(1, 1));
        m.submit(frame(0, 2));
        QVERIFY(m.latest(0).serial > m.latest(1).serial);
        QVERIFY(m.latest(1).serial > 0);
    }

    void outOfRangeIsIgnored()
    {
        LidarMailbox m(1);
        QSignalSpy changed(&m, &LidarMailbox::changed);
        m.submit(frame(3, 1));
        m.submit(frame(-1, 1));
        QCOMPARE(changed.count(), 0);
        QCOMPARE(m.latest(7).serial, quint64(0));
    }

    void emitsChangedWithSensor()
    {
        LidarMailbox m(3);
        QSignalSpy changed(&m, &LidarMailbox::changed);
        m.submit(frame(2, 1));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.first().at(0).toInt(), 2);
    }

    void concurrentWritersAndReaders()
    {
        LidarMailbox m(4);
        std::atomic<bool> stop{false};
        std::atomic<int> violations{0};
        std::vector<std::thread> writers;
        for (int s = 0; s < 4; ++s)
            writers.emplace_back([&, s] {
                for (quint64 i = 1; i <= 20000; ++i)
                    m.submit(frame(s, i));
            });
        std::thread reader([&] {
            std::vector<quint64> lastSeq(4, 0);
            while (!stop) {
                for (int s = 0; s < 4; ++s) {
                    const quint64 seq = m.latest(s).frame.seq;
                    if (seq < lastSeq[size_t(s)])
                        ++violations; // a reader must never see a sensor go backwards
                    lastSeq[size_t(s)] = seq;
                }
            }
        });
        for (auto &w : writers)
            w.join();
        stop = true;
        reader.join();
        QCOMPARE(violations.load(), 0);
        for (int s = 0; s < 4; ++s)
            QCOMPARE(m.latest(s).frame.seq, quint64(20000));
    }
};

QTEST_MAIN(TstMailbox)
#include "tst_mailbox.moc"
