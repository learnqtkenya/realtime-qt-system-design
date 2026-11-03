// How long does a paused seek take to deliver the exact frame, and what decides it?
// Usage: e05_video_seek <video> <decoders> <seeks> > out.csv
// Each decoder is a QMediaPlayer, paused, asked to seek to random times. One row per seek:
// decoder, target ms, ms since the previous keyframe (GOP from argv[4], frames), latency ms.

#include "qp/SimTypes.h"

#include <QGuiApplication>
#include <QMediaPlayer>
#include <QTextStream>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>

#include <memory>
#include <random>
#include <vector>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (argc < 5) {
        QTextStream(stderr) << "usage: e05_video_seek <video> <decoders> <seeks> <gop-frames>\n";
        return 2;
    }
    const QUrl url = QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1]));
    const int decoders = atoi(argv[2]);
    const int seeks = atoi(argv[3]);
    const int gop = atoi(argv[4]);
    QTextStream out(stdout);
    out << "decoder,target_ms,since_keyframe_ms,latency_ms,frame_ms\n";

    struct Decoder
    {
        std::unique_ptr<QMediaPlayer> player = std::make_unique<QMediaPlayer>();
        std::unique_ptr<QVideoSink> sink = std::make_unique<QVideoSink>();
        qint64 requestedAt = 0, target = -1;
        int done = 0;
    };
    std::vector<Decoder> ds(static_cast<size_t>(decoders));
    std::mt19937 rng(42);
    int finished = 0;
    for (int i = 0; i < decoders; ++i) {
        Decoder &d = ds[size_t(i)];
        d.player->setSource(url);
        d.player->setVideoSink(d.sink.get());
        auto next = [&, i] {
            Decoder &d = ds[size_t(i)];
            const qint64 duration = d.player->duration();
            if (d.done >= seeks || duration <= 0) {
                if (++finished == decoders)
                    app.quit();
                return;
            }
            // Whole frames, away from the end; the same sequence for every run.
            d.target = qint64(rng() % quint32(duration / 34 - 30)) * 1000 / 30 + 5;
            d.requestedAt = qp::nowNs();
            d.player->setPosition(d.target);
        };
        QObject::connect(d.sink.get(), &QVideoSink::videoFrameChanged, d.sink.get(), [&, i, next](const QVideoFrame &f) {
            Decoder &d = ds[size_t(i)];
            if (d.target < 0)
                return;
            const qint64 frameMs = f.startTime() / 1000;
            const double latency = double(qp::nowNs() - d.requestedAt) / 1e6;
            const qint64 frameIndex = d.target * 30 / 1000;
            const qint64 sinceKey = (frameIndex % gop) * 1000 / 30;
            out << i << ',' << d.target << ',' << sinceKey << ',' << latency << ',' << frameMs << '\n';
            d.target = -1;
            ++d.done;
            QTimer::singleShot(50, d.sink.get(), next); // let the decoder settle between seeks
        });
        QObject::connect(d.player.get(), &QMediaPlayer::mediaStatusChanged, d.player.get(),
                         [&, i, next](QMediaPlayer::MediaStatus s) {
                             Decoder &d = ds[size_t(i)];
                             if (s == QMediaPlayer::LoadedMedia && d.done == 0 && d.target < 0) {
                                 d.player->pause();
                                 QTimer::singleShot(300, d.sink.get(), next);
                             }
                         });
    }
    QTimer::singleShot(120'000, &app, &QGuiApplication::quit);
    return app.exec();
}
