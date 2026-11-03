#include "qp/RunningSamples.h"

#include <QtTest>

class TstRunningSamples : public QObject
{
    Q_OBJECT
private slots:
    void emptyIsAllZero()
    {
        qp::RunningSamples s;
        QCOMPARE(s.count(), size_t(0));
        QCOMPARE(s.mean(), 0.0);
        QCOMPARE(s.percentile(99), 0.0);
        QCOMPARE(s.max(), 0.0);
    }

    void basicStatistics()
    {
        qp::RunningSamples s;
        for (int i = 1; i <= 5; ++i)
            s.add(i);
        QCOMPARE(s.count(), size_t(5));
        QCOMPARE(s.mean(), 3.0);
        QCOMPARE(s.min(), 1.0);
        QCOMPARE(s.max(), 5.0);
        QCOMPARE(s.percentile(50), 3.0);
        QCOMPARE(s.percentile(0), 1.0);
        QCOMPARE(s.percentile(100), 5.0);
    }

    void percentileInterpolates()
    {
        qp::RunningSamples s;
        for (int i = 0; i <= 100; ++i)
            s.add(i); // 0..100, so the p-th percentile equals p
        QCOMPARE(s.percentile(99), 99.0);
        QCOMPARE(s.percentile(95), 95.0);
        QVERIFY(qFuzzyCompare(s.percentile(99.5), 99.5));
    }

    void percentileIgnoresInsertionOrder()
    {
        qp::RunningSamples a, b;
        for (double v : {5.0, 1.0, 4.0, 2.0, 3.0})
            a.add(v);
        for (double v : {1.0, 2.0, 3.0, 4.0, 5.0})
            b.add(v);
        QCOMPARE(a.percentile(75), b.percentile(75));
    }

    void countAboveIsStrict()
    {
        qp::RunningSamples s;
        for (double v : {10.0, 20.0, 20.0, 30.0})
            s.add(v);
        QCOMPARE(s.countAbove(20.0), size_t(1));
        QCOMPARE(s.countAbove(19.9), size_t(3));
    }

    void clearResets()
    {
        qp::RunningSamples s;
        s.add(7);
        s.clear();
        QCOMPARE(s.count(), size_t(0));
        QCOMPARE(s.mean(), 0.0);
    }

    void jsonHasExpectedKeys()
    {
        qp::RunningSamples s;
        s.add(1);
        s.add(2);
        const QJsonObject o = s.toJson();
        for (const char *k : {"n", "mean", "min", "p50", "p95", "p99", "max"})
            QVERIFY2(o.contains(QLatin1String(k)), k);
        QCOMPARE(o["n"].toDouble(), 2.0);
    }
};

QTEST_APPLESS_MAIN(TstRunningSamples)
#include "tst_runningsamples.moc"
