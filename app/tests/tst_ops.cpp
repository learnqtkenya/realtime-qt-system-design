#include "Diagnostics.h"
#include "LayoutStore.h"
#include "qp/RunningSamples.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QtTest>

class Thing : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString view MEMBER view NOTIFY changed)
    Q_PROPERTY(int delay MEMBER delay NOTIFY changed)
    Q_PROPERTY(QStringList layers MEMBER layers NOTIFY changed)

public:
    QString view = QStringLiteral("top");
    int delay = 0;
    QStringList layers;

signals:
    void changed();
};

class TstOps : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    QString file(const QString &name, const QByteArray &content = {})
    {
        const QString path = m_dir.filePath(name);
        if (!content.isNull()) {
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly))
                qFatal("cannot write %s", qPrintable(path));
            f.write(content);
        }
        return path;
    }

    static QJsonObject read(const QString &path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
    }

private slots:
    void layoutRoundTrip()
    {
        const QString path = file(QStringLiteral("round.json"));
        {
            LayoutStore store(path);
            store.load();
            Thing t;
            store.track(&t, "view", QStringLiteral("view"));
            store.track(&t, "delay", QStringLiteral("delay"));
            store.track(&t, "layers", QStringLiteral("layers"));
            t.view = QStringLiteral("orbit");
            t.delay = 200;
            t.layers = {QStringLiteral("a"), QStringLiteral("b")};
            QVERIFY(store.save());
        }
        LayoutStore store(path);
        store.load();
        QVERIFY(store.status().startsWith(u"restored"));
        Thing t;
        store.track(&t, "view", QStringLiteral("view"));
        store.track(&t, "delay", QStringLiteral("delay"));
        store.track(&t, "layers", QStringLiteral("layers"));
        QCOMPARE(t.view, QStringLiteral("orbit"));
        QCOMPARE(t.delay, 200);
        QCOMPARE(t.layers, (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
    }

    void changesAreSavedAfterASecond()
    {
        const QString path = file(QStringLiteral("debounce.json"));
        LayoutStore store(path);
        store.load();
        Thing t;
        store.track(&t, "delay", QStringLiteral("delay"));
        QSignalSpy saved(&store, &LayoutStore::saved);
        for (int i = 1; i <= 5; ++i) {
            t.setProperty("delay", i * 10);
            emit t.changed();
        }
        QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 1, 3000);
        QTest::qWait(1200);
        QCOMPARE(saved.size(), 1); // five changes, one write
        QCOMPARE(read(path).value(u"values").toObject().value(u"delay").toInt(), 50);
    }

    void commandLineWins()
    {
        const QString path = file(QStringLiteral("locked.json"), R"({"version":1,"values":{"view":"orbit"}})");
        LayoutStore store(path);
        store.load();
        Thing t;
        t.view = QStringLiteral("from-command-line");
        store.track(&t, "view", QStringLiteral("view"), true);
        QCOMPARE(t.view, QStringLiteral("from-command-line"));
        QVERIFY(store.save());
        QCOMPARE(read(path).value(u"values").toObject().value(u"view").toString(), QStringLiteral("orbit"));
    }

    void damagedFileIsSetAside()
    {
        const QString path = file(QStringLiteral("bad.json"), QByteArray("\x00\xff{{ not json", 14));
        LayoutStore store(path);
        store.load();
        QVERIFY(store.status().contains(u"damaged"));
        QVERIFY(!QFile::exists(path));
        const QStringList aside = QDir(m_dir.path()).entryList({QStringLiteral("bad.json.bad-*")});
        QCOMPARE(aside.size(), 1);
        Thing t;
        store.track(&t, "view", QStringLiteral("view"));
        QCOMPARE(t.view, QStringLiteral("top")); // defaults
        QVERIFY(store.save()); // and a good file replaces it
        QCOMPARE(read(path).value(u"version").toInt(), LayoutStore::kVersion);
    }

    void newerFileIsReadButNeverOverwritten()
    {
        const QByteArray newer = R"({"version":99,"values":{"view":"orbit","hologram":true}})";
        const QString path = file(QStringLiteral("newer.json"), newer);
        LayoutStore store(path);
        store.load();
        Thing t;
        store.track(&t, "view", QStringLiteral("view"));
        QCOMPARE(t.view, QStringLiteral("orbit"));
        t.view = QStringLiteral("top");
        QVERIFY(!store.save());
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), newer);
    }

    void unknownKeysSurvive()
    {
        const QString path = file(QStringLiteral("unknown.json"), R"({"version":1,"values":{"view":"top","colourScheme":"dusk"}})");
        LayoutStore store(path);
        store.load();
        Thing t;
        store.track(&t, "view", QStringLiteral("view"));
        t.view = QStringLiteral("orbit");
        QVERIFY(store.save());
        const QJsonObject v = read(path).value(u"values").toObject();
        QCOMPARE(v.value(u"view").toString(), QStringLiteral("orbit"));
        QCOMPARE(v.value(u"colourScheme").toString(), QStringLiteral("dusk"));
    }

    void noPathNoFile()
    {
        LayoutStore store{QString()};
        store.load();
        Thing t;
        store.track(&t, "view", QStringLiteral("view"));
        QVERIFY(!store.save());
    }

    void logRotates()
    {
        const QString dir = m_dir.filePath(QStringLiteral("logs"));
        Diagnostics d;
        d.installLog(dir, 2000, 2);
        for (int i = 0; i < 200; ++i)
            d.note(QStringLiteral("test"), QStringLiteral("line %1 ").arg(i) + QString(40, u'x'));
        qWarning("a warning goes to the log too");
        const QStringList files = QDir(dir).entryList({QStringLiteral("viewer.log*")}, QDir::Files, QDir::Name);
        QCOMPARE(files, (QStringList{QStringLiteral("viewer.log"), QStringLiteral("viewer.log.1"),
                                     QStringLiteral("viewer.log.2")}));
        for (const QString &f : files)
            QVERIFY(QFileInfo(QDir(dir).filePath(f)).size() < 2200);
        QVERIFY(d.recentLog().last().contains(u"a warning goes to the log too"));
        QFile last(QDir(dir).filePath(QStringLiteral("viewer.log")));
        QVERIFY(last.open(QIODevice::ReadOnly));
        for (const QByteArray &line : last.readAll().split('\n'))
            if (!line.isEmpty())
                QVERIFY(QJsonDocument::fromJson(line).isObject()); // every line is JSON
    }

    void metricsAndBundle()
    {
        Diagnostics d;
        d.setBuild({{"version", "test"}});
        d.addSection(QStringLiteral("frames"), [] { return QJsonObject{{"fps", 60}, {"api", "opengl"}, {"slow", false}}; });
        d.start();
        QTRY_VERIFY(d.metricsText().contains("qp_process_rssMb ")); // the first sample, from the sampler thread
        const QByteArray text = d.metricsText();
        QVERIFY(text.contains("qp_frames_fps 60\n"));
        QVERIFY(text.contains("qp_frames_slow 0\n"));
        QVERIFY(!text.contains("opengl")); // only numbers
        QVERIFY(text.contains("qp_process_rssMb "));
        QVERIFY(text.contains("qp_process_threads "));
        const QJsonObject b = d.bundle();
        QCOMPARE(b.value(u"build").toObject().value(u"version").toString(), QStringLiteral("test"));
        QCOMPARE(b.value(u"state").toObject().value(u"frames").toObject().value(u"api").toString(),
                 QStringLiteral("opengl"));
        const QString saved = d.saveBundle(m_dir.filePath(QStringLiteral("b/bundle.json")));
        QVERIFY(!saved.isEmpty());
        QCOMPARE(read(saved).value(u"build").toObject().value(u"version").toString(), QStringLiteral("test"));
    }

    void samplesWindowIsBounded()
    {
        qp::RunningSamples s;
        s.setCapacity(100);
        for (int i = 1; i <= 1000; ++i)
            s.add(i);
        QCOMPARE(s.count(), size_t(100));
        QCOMPARE(s.added(), size_t(1000));
        QCOMPARE(s.mean(), 950.5); // the newest 100: 901..1000
        QCOMPARE(s.min(), 901.0);
        QCOMPARE(s.max(), 1000.0);
        qp::RunningSamples all;
        for (int i = 1; i <= 1000; ++i)
            all.add(i);
        QCOMPARE(all.count(), size_t(1000)); // capacity 0 keeps everything
        all.setCapacity(10);
        QCOMPARE(all.count(), size_t(10));
        QCOMPARE(all.mean(), 995.5);
    }

    void metricsEndpoint()
    {
        Diagnostics d;
        d.start();
        QTRY_VERIFY(d.metricsText().contains("qp_process_uptimeS "));
        QString error;
        QVERIFY2(d.startMetrics(19123, &error), qPrintable(error));
        QTcpSocket s;
        s.connectToHost(QHostAddress::LocalHost, 19123);
        QVERIFY(s.waitForConnected(2000));
        s.write("GET /metrics HTTP/1.0\r\n\r\n");
        QByteArray reply;
        while (s.state() == QAbstractSocket::ConnectedState && s.waitForReadyRead(2000))
            reply += s.readAll();
        reply += s.readAll();
        QVERIFY(reply.startsWith("HTTP/1.0 200 OK"));
        QVERIFY(reply.contains("qp_process_uptimeS "));
    }
};

QTEST_GUILESS_MAIN(TstOps)
#include "tst_ops.moc"
