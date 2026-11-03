// Shape of a stored lidar frame: bytes and CPU per frame for each encoding and compression.
// Usage: bench_codec [points-per-frame] > results/step-04/codec.md

#include "Codec.h"
#include "qp/SensorSimulator.h"

#include <QCoreApplication>
#include <QTextStream>

#include <zstd.h>

#include <algorithm>
#include <functional>
#include <vector>

namespace {

QByteArray shuffle(const QByteArray &in, int width, qsizetype headerBytes)
{
    QByteArray out(in.size(), Qt::Uninitialized);
    std::memcpy(out.data(), in.constData(), size_t(headerBytes));
    const qsizetype n = (in.size() - headerBytes) / width;
    const char *src = in.constData() + headerBytes;
    char *dst = out.data() + headerBytes;
    for (int b = 0; b < width; ++b)
        for (qsizetype i = 0; i < n; ++i)
            dst[b * n + i] = src[i * width + b];
    return out;
}

QByteArray unshuffle(const QByteArray &in, int width, qsizetype headerBytes)
{
    QByteArray out(in.size(), Qt::Uninitialized);
    std::memcpy(out.data(), in.constData(), size_t(headerBytes));
    const qsizetype n = (in.size() - headerBytes) / width;
    const char *src = in.constData() + headerBytes;
    char *dst = out.data() + headerBytes;
    for (int b = 0; b < width; ++b)
        for (qsizetype i = 0; i < n; ++i)
            dst[i * width + b] = src[b * n + i];
    return out;
}

QByteArray zc(const QByteArray &in, int level)
{
    QByteArray out(qsizetype(ZSTD_compressBound(size_t(in.size()))), Qt::Uninitialized);
    const size_t n = ZSTD_compress(out.data(), size_t(out.size()), in.constData(), size_t(in.size()), level);
    out.resize(qsizetype(n));
    return out;
}

QByteArray zd(const QByteArray &in, qsizetype rawSize)
{
    QByteArray out(rawSize, Qt::Uninitialized);
    ZSTD_decompress(out.data(), size_t(rawSize), in.constData(), size_t(in.size()));
    return out;
}

double msOf(const std::function<void()> &f, int reps)
{
    std::vector<double> t;
    for (int i = 0; i < reps; ++i) {
        const qint64 t0 = qp::nowNs();
        f();
        t.push_back(double(qp::nowNs() - t0) / 1e6);
    }
    std::sort(t.begin(), t.end());
    return t[t.size() / 2];
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    qp::Workload w;
    w.pointsPerFrame = argc > 1 ? atoi(argv[1]) : 150'000;
    std::vector<qp::SensorFrame> frames;
    for (int s = 0; s < 4; ++s)
        for (int v = 0; v < 4; ++v)
            frames.push_back(qp::SensorFrame{s, quint64(v), 0, qp::SensorSimulator::makeCloud(w, s, v)});

    QTextStream out(stdout);
    out << "# Lidar frame encodings (" << w.pointsPerFrame << " points per frame)\n\n"
        << "Median over " << frames.size() << " simulator frames (4 sensors × 4 variants), one thread. "
        << "MB/s are of raw float32 input (" << w.pointsPerFrame * 12 / 1e6 << " MB per frame).\n\n"
        << "| Encoding | Bytes per point | Ratio vs float32 | Encode ms/frame | Decode ms/frame | Encode MB/s | "
           "Nominal load (40 frames/s): encode CPU |\n|---|---|---|---|---|---|---|\n";

    struct Variant
    {
        const char *name;
        rec::LidarEncoding enc;
        int shuffleWidth; // 0: none
        int zstdLevel;    // 0: none
    };
    const Variant variants[] = {
        {"float32", rec::LidarEncoding::Float32, 0, 0},
        {"float32 + zstd 1", rec::LidarEncoding::Float32, 0, 1},
        {"float32 + shuffle + zstd 1", rec::LidarEncoding::Float32, 4, 1},
        {"int16 (1 cm)", rec::LidarEncoding::Int16, 0, 0},
        {"int16 + zstd 1", rec::LidarEncoding::Int16, 0, 1},
        {"int16 + shuffle + zstd 1", rec::LidarEncoding::Int16, 2, 1},
        {"int16 + shuffle + zstd 3", rec::LidarEncoding::Int16, 2, 3},
        {"int16 + shuffle + zstd 9", rec::LidarEncoding::Int16, 2, 9},
        {"int16 planar (one pass)", rec::LidarEncoding::Int16Planar, 0, 0},
        {"int16 planar (one pass) + zstd 1", rec::LidarEncoding::Int16Planar, 0, 1},
    };
    const double rawBytes = double(w.pointsPerFrame) * 12;
    for (const Variant &v : variants) {
        const qsizetype header = v.enc == rec::LidarEncoding::Float32 ? 12 : 16;
        std::vector<double> enc, dec, sizes;
        double maxErr = 0;
        for (const qp::SensorFrame &f : frames) {
            QByteArray stored;
            enc.push_back(msOf([&] {
                QByteArray b = rec::encodeLidar(f, v.enc);
                if (v.shuffleWidth)
                    b = shuffle(b, v.shuffleWidth, header);
                stored = v.zstdLevel ? zc(b, v.zstdLevel) : b;
            }, 3));
            const qsizetype raw = rec::encodeLidar(f, v.enc).size();
            sizes.push_back(double(stored.size()));
            std::optional<qp::SensorFrame> back;
            dec.push_back(msOf([&] {
                QByteArray b = v.zstdLevel ? zd(stored, raw) : stored;
                if (v.shuffleWidth)
                    b = unshuffle(b, v.shuffleWidth, header);
                back = rec::decodeLidar(quint16(v.enc), f.sensorId, 0, b.constData(), size_t(b.size()));
            }, 3));
            if (!back || back->cloud->xyz.size() != f.cloud->xyz.size())
                qFatal("round trip failed for %s", v.name);
            for (size_t i = 0; i < f.cloud->xyz.size(); ++i)
                maxErr = std::max(maxErr, double(std::abs(back->cloud->xyz[i] - f.cloud->xyz[i])));
        }
        std::sort(enc.begin(), enc.end());
        std::sort(dec.begin(), dec.end());
        double size = 0;
        for (double s : sizes)
            size += s;
        size /= double(sizes.size());
        const double e = enc[enc.size() / 2], d = dec[dec.size() / 2];
        out << "| " << v.name << " | " << QString::number(size / w.pointsPerFrame, 'f', 2) << " | "
            << QString::number(rawBytes / size, 'f', 2) << " | " << QString::number(e, 'f', 2) << " | "
            << QString::number(d, 'f', 2) << " | " << QString::number(rawBytes / 1e6 / (e / 1000), 'f', 0) << " | "
            << QString::number(40 * e / 10, 'f', 1) << "% of a core |\n";
        if (maxErr > 0)
            out << "|   max error " << QString::number(maxErr * 1000, 'f', 2) << " mm | | | | | | |\n";
    }
    return 0;
}
