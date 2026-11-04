#include "LayoutStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QMetaProperty>
#include <QSaveFile>

Q_LOGGING_CATEGORY(lcLayout, "qp.layout")

LayoutStore::LayoutStore(const QString &path, QObject *parent) : QObject(parent), m_path(path)
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(1000);
    connect(&m_saveTimer, &QTimer::timeout, this, &LayoutStore::save);
}

LayoutStore::~LayoutStore()
{
    if (m_saveTimer.isActive())
        save();
}

void LayoutStore::setStatus(const QString &s)
{
    if (s == m_status)
        return;
    m_status = s;
    emit statusChanged();
}

void LayoutStore::load()
{
    if (m_path.isEmpty()) {
        setStatus(QStringLiteral("not saved (benchmark or --set layout=0)"));
        return;
    }
    QFile f(m_path);
    if (!f.exists()) {
        setStatus(QStringLiteral("defaults (no saved layout yet)"));
        return;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        m_readOnly = true;
        setStatus(QStringLiteral("defaults: cannot read %1").arg(m_path));
        qCWarning(lcLayout) << "cannot read" << m_path << f.errorString();
        return;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    f.close();
    if (pe.error != QJsonParseError::NoError || !doc.isObject() || !doc.object().value(u"values").isObject()) {
        const QString aside = m_path + QStringLiteral(".bad-")
                              + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        QFile::rename(m_path, aside);
        setStatus(QStringLiteral("defaults: the saved layout was damaged (kept as %1)").arg(QFileInfo(aside).fileName()));
        qCWarning(lcLayout) << "damaged layout file set aside as" << aside << pe.errorString();
        return;
    }
    m_file = doc.object();
    const int version = m_file.value(u"version").toInt();
    if (version > kVersion) {
        m_readOnly = true;
        setStatus(QStringLiteral("from a newer viewer (version %1): read, never overwritten").arg(version));
        qCWarning(lcLayout) << "layout file from version" << version << "; this viewer writes" << kVersion
                            << "and will not overwrite it";
    } else {
        setStatus(QStringLiteral("restored from %1").arg(m_path));
    }
}

void LayoutStore::track(QObject *object, const char *property, const QString &key, bool locked)
{
    const QMetaObject *mo = object->metaObject();
    const int index = mo->indexOfProperty(property);
    if (index < 0) {
        qCWarning(lcLayout) << "no property" << property << "on" << object;
        return;
    }
    const QMetaProperty p = mo->property(index);
    const QJsonValue saved = m_file.value(u"values").toObject().value(key);
    if (!locked && !saved.isUndefined()) {
        QVariant v = saved.toVariant();
        if (v.convert(p.metaType())) {
            m_applying = true;
            p.write(object, v);
            m_applying = false;
        }
    }
    m_tracked.push_back({object, property, key, locked});
    if (p.hasNotifySignal()) {
        const int slot = metaObject()->indexOfMethod("changed()");
        QMetaObject::connect(object, p.notifySignalIndex(), this, slot);
    }
}

void LayoutStore::changed()
{
    if (!m_applying && !m_path.isEmpty() && !m_readOnly)
        m_saveTimer.start();
}

bool LayoutStore::save()
{
    m_saveTimer.stop();
    if (m_path.isEmpty() || m_readOnly)
        return false;
    QJsonObject values = m_file.value(u"values").toObject(); // keys we don't know survive
    for (const Tracked &t : m_tracked)
        if (t.object && !t.locked)
            values[t.key] = QJsonValue::fromVariant(t.object->property(t.property.constData()));
    m_file[u"version"] = kVersion;
    m_file[u"values"] = values;
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(m_file).toJson()) < 0 || !f.commit()) {
        qCWarning(lcLayout) << "cannot save layout to" << m_path << f.errorString();
        return false;
    }
    emit saved();
    return true;
}

void LayoutStore::setDataPanel(bool on)
{
    if (on == m_dataPanel)
        return;
    m_dataPanel = on;
    emit dataPanelChanged();
}

void LayoutStore::setLabPanel(bool on)
{
    if (on == m_labPanel)
        return;
    m_labPanel = on;
    emit labPanelChanged();
}
