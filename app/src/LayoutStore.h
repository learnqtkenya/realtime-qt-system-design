#pragma once

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QTimer>

#include <vector>

// The viewer as the user left it: chosen properties of the viewer's objects, saved to a JSON file
// a second after they change and at exit, and applied at the next start. Written atomically.
// A damaged file is set aside; one from a newer version is read but never overwritten; keys this
// version doesn't know are kept.
class LayoutStore : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Workspace)
    QML_SINGLETON
    QML_UNCREATABLE("Provided by main() via QQmlEngine::setExternalSingletonInstance")

    Q_PROPERTY(bool dataPanel READ dataPanel WRITE setDataPanel NOTIFY dataPanelChanged FINAL)
    Q_PROPERTY(bool labPanel READ labPanel WRITE setLabPanel NOTIFY labPanelChanged FINAL)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged FINAL)
    Q_PROPERTY(QString path READ path CONSTANT FINAL)

public:
    static constexpr int kVersion = 1;

    // path empty: nothing is read or written (benchmarks start from the same state every time).
    explicit LayoutStore(const QString &path, QObject *parent = nullptr);
    ~LayoutStore() override;

    void load();
    // Saved under key; the saved value is applied now. Locked (set on the command line): neither
    // applied nor saved, so a one-off option doesn't replace the user's choice.
    void track(QObject *object, const char *property, const QString &key, bool locked = false);
    bool save();

    bool dataPanel() const { return m_dataPanel; }
    void setDataPanel(bool on);
    bool labPanel() const { return m_labPanel; }
    void setLabPanel(bool on);
    QString status() const { return m_status; }
    QString path() const { return m_path; }
    QJsonObject contents() const { return m_file; }

signals:
    void dataPanelChanged();
    void labPanelChanged();
    void statusChanged();
    void saved();

private slots:
    void changed();

private:
    void setStatus(const QString &s);

    struct Tracked
    {
        QPointer<QObject> object;
        QByteArray property;
        QString key;
        bool locked; // set on the command line: for this run only, never saved
    };
    QString m_path;
    QJsonObject m_file;
    bool m_readOnly = false;
    bool m_applying = false;
    std::vector<Tracked> m_tracked;
    QTimer m_saveTimer;
    QString m_status;
    bool m_dataPanel = true;
    bool m_labPanel = true;
};
