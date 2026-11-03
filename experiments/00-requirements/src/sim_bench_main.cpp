#include "qp/AppOptions.h"
#include "qp/RunningSamples.h"
#include "qp/SensorSimulator.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSysInfo>
#include <QTextStream>
#include <QTimer>
#include <QVariantList>
#include <QVector3D>

#include <algorithm>
#include <cstring>
#include <vector>

namespace {

double medianOf(std::vector<double> v)
{
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

QJsonObject benchGeneration()
{
    QJsonArray rows;
    for (int points : {10'000, 50'000, 150'000, 300'000}) {
        qp::Workload w;
        w.pointsPerFrame = points;
        std::vector<double> ms;
        for (int rep = 0; rep < 7; ++rep) {
            const int64_t t0 = qp::nowNs();
            auto c = qp::SensorSimulator::makeCloud(w, 0, rep);
            ms.push_back(double(qp::nowNs() - t0) / 1e6);
            if (c->count() != points)
                qFatal("bad cloud");
        }
        const double med = medianOf(ms);
        QJsonObject r;
        r["points"] = points;
        r["bytes"] = points * 12;
        r["medianMs"] = med;
        r["nsPerPoint"] = med * 1e6 / points;
        rows.append(r);
    }
    return {{"rows", rows}};
}

QJsonObject benchMemcpy()
{
    const size_t bytes = size_t(256) << 20; // well above the 16 MiB L3
    std::vector<char> src(bytes, 1), dst(bytes, 0);
    std::vector<double> gbps;
    for (int rep = 0; rep < 6; ++rep) {
        const int64_t t0 = qp::nowNs();
        std::memcpy(dst.data(), src.data(), bytes);
        const double s = double(qp::nowNs() - t0) / 1e9;
        gbps.push_back(double(bytes) / s / 1e9);
    }
    gbps.erase(gbps.begin()); // first run pays for page faults
    QJsonObject o;
    o["bufferMiB"] = 256;
    o["medianGBps"] = medianOf(gbps);
    o["bestGBps"] = *std::max_element(gbps.begin(), gbps.end());
    o["note"] = "payload GB/s; memory traffic is about twice this (read + write)";
    return o;
}

QJsonObject benchVariantConversion()
{
    QJsonArray rows;
    for (int points : {10'000, 50'000, 150'000}) {
        qp::Workload w;
        w.pointsPerFrame = points;
        auto cloud = qp::SensorSimulator::makeCloud(w, 0, 0);
        std::vector<double> ms;
        for (int rep = 0; rep < 7; ++rep) {
            const int64_t t0 = qp::nowNs();
            QVariantList list;
            list.reserve(points);
            for (int i = 0; i < points; ++i)
                list.append(QVector3D(cloud->xyz[size_t(3 * i)], cloud->xyz[size_t(3 * i + 1)],
                                      cloud->xyz[size_t(3 * i + 2)]));
            ms.push_back(double(qp::nowNs() - t0) / 1e6);
            if (list.size() != points)
                qFatal("bad list");
        }
        const double med = medianOf(ms);
        QJsonObject r;
        r["points"] = points;
        r["medianMs"] = med;
        r["nsPerPoint"] = med * 1e6 / points;
        rows.append(r);
    }
    return {{"rows", rows}};
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    qp::AppOptions *opts = qp::AppOptions::instance();
    if (!opts->parse(app.arguments()))
        return 2;
    QTextStream out(stdout);

    QJsonObject report;
    report["qtVersion"] = QString::fromLatin1(qVersion());
    report["kernel"] = QSysInfo::kernelVersion();
    out << "generation...\n" << Qt::flush;
    report["generation"] = benchGeneration();
    out << "memcpy...\n" << Qt::flush;
    report["memcpy"] = benchMemcpy();
    out << "variant conversion...\n" << Qt::flush;
    report["variantConversion"] = benchVariantConversion();

    out << "delivery (" << opts->durationSec() << " s)...\n" << Qt::flush;
    qp::Workload w = opts->workload();
    w.sensors = 4;
    w.pointsPerFrame = 150'000;
    w.lidarRateHz = 10;
    qp::SensorSimulator sim(w);

    std::vector<qp::RunningSamples> interArrivalMs(4);
    std::vector<int64_t> lastCapture(4, 0);
    qp::RunningSamples deliveryMs;
    quint64 received = 0;
    QObject::connect(&sim, &qp::SensorSimulator::lidarFrame, &app, [&](qp::SensorFrame f) {
        ++received;
        deliveryMs.add(double(qp::nowNs() - f.captureNs) / 1e6);
        int64_t &last = lastCapture[size_t(f.sensorId)];
        if (last > 0)
            interArrivalMs[size_t(f.sensorId)].add(double(f.captureNs - last) / 1e6);
        last = f.captureNs;
    });
    sim.start();
    const double seconds = opts->durationSec();
    QTimer::singleShot(int(seconds * 1000), &app, &QCoreApplication::quit);
    app.exec();
    sim.stop();

    const double expected = 4 * 10 * seconds;
    QJsonObject d;
    d["seconds"] = seconds;
    d["expectedFrames"] = expected;
    d["receivedFrames"] = double(received);
    d["deliveredFraction"] = double(received) / expected;
    d["deliveryMs"] = deliveryMs.toJson();
    qp::RunningSamples all;
    for (const auto &s : interArrivalMs)
        for (double v : s.values())
            all.add(v);
    d["interArrivalMs"] = all.toJson();
    d["interArrivalJitterMs"] = qMax(all.max() - 100.0, 100.0 - all.min());
    report["delivery"] = d;

    QFileInfo fi(opts->outPath());
    QDir().mkpath(fi.absolutePath());
    QFile f(opts->outPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return 1;
    f.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
    out << "wrote " << opts->outPath() << '\n';
    return 0;
}
