#include "FrameStore.h"
#include "Overlay.h"
#include "Plugins.h"
#include "Wire.h"
#include "qp/StageStats.h"

#include <QElapsedTimer>
#include <QProcess>
#include <QtTest>

#include <cmath>

namespace {

qp::TrackSetPtr tracks(int n, qint64 t)
{
    auto set = std::make_shared<qp::TrackSet>();
    set->captureNs = t;
    for (int i = 0; i < n; ++i)
        set->tracks.push_back({quint32(1000 + i), float(i), float(-i), 0.5f, 3.0f, 4, 2, 0});
    return set;
}

} // namespace

class TstPlugins : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    QString plugin(const QString &id, const QByteArray &manifest, const QString &file = {}, const QByteArray &code = {})
    {
        const QString d = m_dir.filePath(QStringLiteral("p/") + id);
        QDir().mkpath(d);
        QFile m(d + QStringLiteral("/plugin.json"));
        m.open(QIODevice::WriteOnly);
        m.write(manifest);
        if (!file.isEmpty()) {
            QFile f(d + u'/' + file);
            f.open(QIODevice::WriteOnly);
            f.write(code);
        }
        return d;
    }

    QByteArray script(const QString &id, const QByteArray &api = "1.0", const QByteArray &entry = "layer.mjs")
    {
        return "{\"id\":\"" + id.toUtf8() + "\",\"kind\":\"script\",\"api\":\"" + api + "\",\"entry\":\"" + entry + "\"}";
    }

    struct Rig
    {
        FrameStore store{1};
        std::unique_ptr<PluginHost> host;
        qint64 t = 1'000'000'000;
        void present(int n = 10)
        {
            t += 100'000'000;
            store.setTracks(tracks(n, t));
        }
        Layer::Status status(const QString &id) const { return host->layer(id)->status(); }
        OverlayFramePtr drawn(const QString &id) const
        {
            for (const auto &d : host->drawn())
                if (d.layer->manifest().id == id)
                    return d.frame;
            return nullptr;
        }
    };

    std::unique_ptr<Rig> rig(const QStringList &dirs, PluginHost::Options o = {})
    {
        auto r = std::make_unique<Rig>();
        o.dirs = dirs;
        r->host = std::make_unique<PluginHost>(&r->store, nullptr, nullptr, o, nullptr);
        r->host->start();
        return r;
    }

private slots:
    void initTestCase() { QVERIFY(m_dir.isValid()); }

    void manifestChecks_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QString>("error");
        QTest::newRow("ok") << script("ok") << QString();
        QTest::newRow("not json") << QByteArray("{ \"id\": ") << QStringLiteral("plugin.json:");
        QTest::newRow("bad id") << script("Bad Id") << QStringLiteral("id must be");
        QTest::newRow("bad api") << QByteArray("{\"id\":\"x\",\"kind\":\"script\",\"api\":\"one\",\"entry\":\"layer.mjs\"}")
                                 << QStringLiteral("api must be");
        QTest::newRow("bad kind") << QByteArray("{\"id\":\"x\",\"kind\":\"dll\",\"api\":\"1.0\",\"entry\":\"layer.mjs\"}")
                                  << QStringLiteral("kind must be");
        QTest::newRow("escaping entry") << script("x", "1.0", "../../etc/passwd") << QStringLiteral("entry must name");
        QTest::newRow("missing entry") << script("x", "1.0", "nothere.mjs") << QStringLiteral("entry must name");
        QTest::newRow("script as exe") << script("x", "1.0", "plugin.json") << QStringLiteral("a script's entry");
        QTest::newRow("process without exec") << QByteArray("{\"id\":\"x\",\"kind\":\"process\",\"api\":\"1.0\"}")
                                              << QStringLiteral("exec is empty");
    }

    void manifestChecks()
    {
        QFETCH(QByteArray, json);
        QFETCH(QString, error);
        const QString d = plugin(QStringLiteral("m-") + QString::fromLatin1(QTest::currentDataTag()).replace(u' ', u'-'),
                                 json, QStringLiteral("layer.mjs"), "export function update() {}");
        const PluginManifest m = PluginManifest::load(d);
        if (error.isEmpty())
            QVERIFY2(m.error.isEmpty(), qPrintable(m.error));
        else
            QVERIFY2(m.error.startsWith(error), qPrintable(m.error));
    }

    void apiGate()
    {
        const QString root = m_dir.filePath(QStringLiteral("gate"));
        const auto make = [&](const QString &id, const QByteArray &api) {
            const QString d = root + u'/' + id;
            QDir().mkpath(d);
            QFile m(d + QStringLiteral("/plugin.json"));
            m.open(QIODevice::WriteOnly);
            m.write(script(id, api));
            QFile f(d + QStringLiteral("/layer.mjs"));
            f.open(QIODevice::WriteOnly);
            f.write("export function update() {}");
        };
        make(QStringLiteral("v10"), "1.0");
        make(QStringLiteral("v11"), "1.1");
        make(QStringLiteral("v12"), "1.2");
        make(QStringLiteral("v20"), "2.0");
        make(QStringLiteral("v09"), "0.9");
        auto r = rig({root});
        QCOMPARE_NE(r->status(QStringLiteral("v10")), Layer::Status::Refused);
        QCOMPARE_NE(r->status(QStringLiteral("v11")), Layer::Status::Refused);
        for (const char *id : {"v12", "v20", "v09"}) {
            QCOMPARE(r->status(QString::fromLatin1(id)), Layer::Status::Refused);
            QVERIFY(r->host->layer(QString::fromLatin1(id))->detail().contains(u"this viewer offers 1.1"));
        }
    }

    void overlayRoundTrip()
    {
        OverlayFrame f;
        f.segments.push_back({1, 2, 3, 4, 0xFF112233});
        f.labels.push_back({5, 6, 0x80FFFFFF, QStringLiteral("zone · 3 inside")});
        const QByteArray b = overlay::encode(f);
        const OverlayFramePtr g = overlay::decode(overlay::kVersion, 9, b.constData(), size_t(b.size()));
        QVERIFY(g);
        QCOMPARE(g->t, 9);
        QCOMPARE(g->segments.size(), size_t(1));
        QCOMPARE(g->segments[0].y2, 4.f);
        QCOMPARE(g->segments[0].rgba, 0xFF112233u);
        QCOMPARE(g->labels[0].text, QStringLiteral("zone · 3 inside"));
    }

    // A newer writer appends fields to segments and labels; this reader takes what it knows.
    void overlayForwardCompatible()
    {
        QByteArray b;
        rec::ByteWriter w(b);
        w.put<quint32>(1);
        w.put<quint16>(24); // stride: one extra field
        for (float v : {1.f, 2.f, 3.f, 4.f})
            w.put<float>(v);
        w.put<quint32>(0xFFFFFFFF);
        w.put<float>(0.5f); // width, from the future
        w.put<quint32>(1);
        const QByteArray text("hi");
        w.put<quint16>(quint16(14 + text.size() + 4));
        w.put<float>(7);
        w.put<float>(8);
        w.put<quint32>(0xFFFFFFFF);
        w.put<quint16>(quint16(text.size()));
        w.bytes(text.constData(), size_t(text.size()));
        w.put<quint32>(42); // font size, from the future
        const OverlayFramePtr g = overlay::decode(2, 0, b.constData(), size_t(b.size()));
        QVERIFY(g);
        QCOMPARE(g->segments[0].x2, 3.f);
        QCOMPARE(g->labels[0].text, QStringLiteral("hi"));
        QCOMPARE(g->labels[0].y, 8.f);
    }

    void overlayRejectsMalformed()
    {
        OverlayFrame f;
        f.segments.push_back({1, 2, 3, std::nanf(""), 0});
        QByteArray b = overlay::encode(f);
        QVERIFY(!overlay::decode(1, 0, b.constData(), size_t(b.size())));
        f.segments.assign(overlay::kMaxSegments + 1, {0, 0, 1, 1, 0});
        b = overlay::encode(f);
        QVERIFY(!overlay::decode(1, 0, b.constData(), size_t(b.size())));
        b = overlay::encode(OverlayFrame{});
        QVERIFY(!overlay::decode(1, 0, b.constData(), size_t(b.size() - 1)));
    }

    void builderLimitsAndColours()
    {
        OverlayBuilder d;
        d.color(QStringLiteral("#80FF0000"));
        d.line(0, 0, 1, 1);
        d.line(0, 0, qQNaN(), 1);
        d.circle(0, 0, 5);
        const OverlayFramePtr f = d.take(3);
        QCOMPARE(f->segments.size(), size_t(1 + 48));
        QCOMPARE(f->segments[0].rgba, 0x80FF0000u);
        QCOMPARE(d.overLimit(), 1u);
    }

    void parserDecodesOnlyNewest()
    {
        QByteArray stream = wire::hello({});
        for (int i = 0; i < 3; ++i) {
            OverlayFrame f;
            f.t = i;
            f.segments.assign(size_t(10 + i), {0, 0, 1, 1, 0});
            stream += wire::overlayRecord(f);
        }
        wire::Parser p;
        p.setLatestOnly(rec::Type::Overlay);
        std::vector<OverlayFramePtr> out;
        QVERIFY(p.feed(stream.constData(), stream.size(), [&](const wire::Decoded &d) { out.push_back(d.overlay); }));
        QCOMPARE(out.size(), size_t(1));
        QCOMPARE(out[0]->t, 2);
        QCOMPARE(p.superseded(), 2);
    }

    void scriptDelivers_data()
    {
        QTest::addColumn<bool>("onGui");
        QTest::addColumn<QByteArray>("entry");
        QTest::newRow("JS module, own thread") << false << QByteArray("layer.mjs");
        QTest::newRow("JS module, GUI thread") << true << QByteArray("layer.mjs");
        QTest::newRow("QML object, own thread") << false << QByteArray("Layer.qml");
    }

    void scriptDelivers()
    {
        QFETCH(bool, onGui);
        QFETCH(QByteArray, entry);
        const QString id = QStringLiteral("deliver-%1-%2").arg(onGui).arg(entry.endsWith(".qml"));
        const QByteArray body = "for (const t of input.tracks) { draw.color('#FF0000'); draw.line(t.x, t.y, t.x + 1, t.y); }"
                                " draw.text(0, 0, 'n=' + input.tracks.length + ' sel=' + input.selectedId);";
        const QByteArray code = entry.endsWith(".qml") ? "import QtQml\nQtObject { function update(input, draw) { " + body + " } }"
                                                        : "export function update(input, draw) { " + body + " }";
        plugin(id, script(id, "1.0", entry), QString::fromLatin1(entry), code);
        PluginHost::Options o;
        o.scriptOnGui = onGui;
        auto r = rig({m_dir.filePath(QStringLiteral("p"))}, o);
        r->host->setEnabled(id, true);
        QTRY_COMPARE(r->status(id), Layer::Status::Ok);
        r->present(25);
        QTRY_VERIFY(r->drawn(id));
        const OverlayFramePtr f = r->drawn(id);
        QCOMPARE(f->t, r->t);
        QCOMPARE(f->segments.size(), size_t(25));
        QCOMPARE(f->segments[3].x1, 3.f);
        QCOMPARE(f->labels[0].text, QStringLiteral("n=25 sel=-1"));
    }

    void scriptInterruptedOffTheGuiThread()
    {
        plugin(QStringLiteral("spin"), script(QStringLiteral("spin")), QStringLiteral("layer.mjs"),
               "let n = 0; export function update(input, draw) { if (++n > 1) { while (true) {} } }");
        PluginHost::Options o;
        o.hardMs = 100;
        auto r = rig({m_dir.filePath(QStringLiteral("p"))}, o);
        r->host->setEnabled(QStringLiteral("spin"), true);
        QTRY_COMPARE(r->status(QStringLiteral("spin")), Layer::Status::Ok);
        r->present();
        QTest::qWait(50);
        QElapsedTimer gui;
        gui.start();
        r->present(); // this one loops; the GUI thread doesn't wait for it
        QVERIFY(gui.elapsed() < 20);
        QTRY_COMPARE_WITH_TIMEOUT(r->status(QStringLiteral("spin")), Layer::Status::Stopped, 2000);
        QVERIFY(r->host->layer(QStringLiteral("spin"))->detail().contains(u"interrupted"));
        r->host->setEnabled(QStringLiteral("spin"), false); // and it can be torn down
    }

    void scriptInterruptedOnTheGuiThread()
    {
        plugin(QStringLiteral("spin2"), script(QStringLiteral("spin2")), QStringLiteral("layer.mjs"),
               "export function update(input, draw) { while (true) {} }");
        PluginHost::Options o;
        o.scriptOnGui = true;
        o.hardMs = 100;
        auto r = rig({m_dir.filePath(QStringLiteral("p"))}, o);
        r->host->setEnabled(QStringLiteral("spin2"), true);
        QElapsedTimer stall;
        stall.start();
        r->present();
        const qint64 ms = stall.elapsed();
        QVERIFY2(ms >= 95 && ms < 400, qPrintable(QString::number(ms)));
        QCOMPARE(r->status(QStringLiteral("spin2")), Layer::Status::Stopped);
    }

    void slowScriptSuspendedOnTheGuiThread()
    {
        plugin(QStringLiteral("slow"), script(QStringLiteral("slow")), QStringLiteral("layer.mjs"),
               "export function update(input, draw) { const end = Date.now() + 10; while (Date.now() < end) {} }");
        PluginHost::Options o;
        o.scriptOnGui = true;
        o.budgetMs = 4;
        auto r = rig({m_dir.filePath(QStringLiteral("p"))}, o);
        r->host->setEnabled(QStringLiteral("slow"), true);
        for (int i = 0; i < 2; ++i) {
            r->present();
            QCOMPARE(r->status(QStringLiteral("slow")), Layer::Status::Ok);
        }
        r->present();
        QCOMPARE(r->status(QStringLiteral("slow")), Layer::Status::Suspended);
        r->present(); // not called again
        QCOMPARE(r->status(QStringLiteral("slow")), Layer::Status::Suspended);
    }

    void jsEngineOffersNothingButTheApi()
    {
        plugin(QStringLiteral("probe"), script(QStringLiteral("probe")), QStringLiteral("layer.mjs"),
               "export function update(input, draw) { draw.text(0, 0, [typeof Qt, typeof XMLHttpRequest, typeof "
               "console].join(',')); }");
        auto r = rig({m_dir.filePath(QStringLiteral("p"))});
        r->host->setEnabled(QStringLiteral("probe"), true);
        QTRY_COMPARE(r->status(QStringLiteral("probe")), Layer::Status::Ok);
        r->present();
        QTRY_VERIFY(r->drawn(QStringLiteral("probe")));
        QCOMPARE(r->drawn(QStringLiteral("probe"))->labels[0].text, QStringLiteral("undefined,undefined,object"));
    }

    void processDelivers()
    {
        auto r = rig({QStringLiteral(PLUGINS_DIR)});
        for (const char *id : {"speed-vectors-py", "speed-vectors-cpp"}) {
            r->host->setEnabled(QString::fromLatin1(id), true);
            QTRY_COMPARE_WITH_TIMEOUT(r->status(QString::fromLatin1(id)), Layer::Status::Ok, 5000);
        }
        r->present(30);
        for (const char *id : {"speed-vectors-py", "speed-vectors-cpp"}) {
            QTRY_VERIFY_WITH_TIMEOUT(r->drawn(QString::fromLatin1(id)), 5000);
            const OverlayFramePtr f = r->drawn(QString::fromLatin1(id));
            QCOMPARE(f->t, r->t);
            QCOMPARE(f->segments.size(), size_t(30));
            // 3 m/s, heading 0.5 rad, 2 s ahead
            QVERIFY(std::abs(f->segments[0].x2 - 6 * std::cos(0.5f)) < 1e-4);
            QVERIFY(std::abs(f->segments[0].y2 - 6 * std::sin(0.5f)) < 1e-4);
        }
    }

    void processRestartsAfterCrashThenGivesUp()
    {
        const QString d = m_dir.filePath(QStringLiteral("crash"));
        QDir().mkpath(d + QStringLiteral("/c"));
        QFile m(d + QStringLiteral("/c/plugin.json"));
        m.open(QIODevice::WriteOnly);
        m.write(QByteArray("{\"id\":\"c\",\"kind\":\"process\",\"api\":\"1.0\",\"exec\":[\"python3\",\"-I\",\"") + PLUGINS_LAB_DIR
                + "/misbehave.py\",\"crash\",\"2\"]}");
        m.close();
        auto r = rig({d});
        QSignalSpy changes(r->host.get(), &PluginHost::layersChanged);
        r->host->setEnabled(QStringLiteral("c"), true);
        QList<Layer::Status> seen;
        QElapsedTimer t;
        t.start();
        while (r->status(QStringLiteral("c")) != Layer::Status::Failed && t.elapsed() < 20'000) {
            r->present();
            QTest::qWait(20);
            if (seen.isEmpty() || seen.last() != r->status(QStringLiteral("c")))
                seen << r->status(QStringLiteral("c"));
        }
        QCOMPARE(r->status(QStringLiteral("c")), Layer::Status::Failed);
        QCOMPARE(seen.count(Layer::Status::Crashed), 4);
        QVERIFY(r->host->layer(QStringLiteral("c"))->detail().contains(u"5 times in a minute"));
    }

    void processFloodIsCutOff()
    {
        auto r = rig({QStringLiteral(PLUGINS_LAB_DIR)});
        r->host->setEnabled(QStringLiteral("flood-process"), true);
        QTRY_VERIFY_WITH_TIMEOUT(r->host->layer(QStringLiteral("flood-process"))->detail().startsWith(u"flooding"), 5000);
    }

    // A plugin written against API 1.0 keeps working when the viewer sends what 1.0 never knew:
    // record types it doesn't know, and tracks with fields appended.
    void pythonPluginSkipsWhatItDoesNotKnow()
    {
        QProcess p;
        p.start(QStringLiteral("python3"), {QStringLiteral("-I"), QStringLiteral(PLUGINS_DIR "/speed-vectors-py/layer.py")});
        QVERIFY(p.waitForStarted());
        QByteArray in = wire::hello({{"api", "1.9"}});
        in += wire::record(rec::Type(99), 1, 0, 1, QByteArray(17, 'x'));
        QByteArray payload;
        rec::ByteWriter w(payload);
        w.put<quint64>(1);
        w.put<quint32>(2);
        w.put<quint16>(40); // 29 known bytes and 11 from the future
        for (int i = 0; i < 2; ++i) {
            w.put<quint32>(quint32(i));
            for (float v : {float(i), 0.f, 0.f, 5.f, 4.f, 2.f})
                w.put<float>(v);
            w.put<quint8>(0);
            w.bytes("future-data", 11);
        }
        in += wire::record(rec::Type::Tracks, 2, 0, 77, payload);
        p.write(in);
        wire::Parser parser;
        OverlayFramePtr got;
        QElapsedTimer t;
        t.start();
        while (!got && t.elapsed() < 5000 && p.waitForReadyRead(5000)) {
            const QByteArray out = p.readAllStandardOutput();
            QVERIFY(parser.feed(out.constData(), out.size(), [&](const wire::Decoded &d) { got = d.overlay; }));
        }
        p.closeWriteChannel();
        p.waitForFinished();
        QVERIFY(got);
        QCOMPARE(got->t, 77);
        QCOMPARE(got->segments.size(), size_t(2));
        QCOMPARE(got->segments[1].x1, 1.f);
        QCOMPARE(got->segments[1].x2, 11.f);
    }
};

QTEST_GUILESS_MAIN(TstPlugins)
#include "tst_plugins.moc"
