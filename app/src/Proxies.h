#pragma once

#include <QQmlEngine>
#include <QAbstractListModel>
#include <QSortFilterProxyModel>

#include <deque>

class EventLogModel;

// The objects table: sort by a role, filter by class.
class TrackTableProxy : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by Scene")

    Q_PROPERTY(QString sortKey READ sortKey WRITE setSortKey NOTIFY sortChanged FINAL)
    Q_PROPERTY(bool descending READ descending WRITE setDescending NOTIFY sortChanged FINAL)
    Q_PROPERTY(int classFilter READ classFilter WRITE setClassFilter NOTIFY classFilterChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    Q_PROPERTY(int total READ total NOTIFY countChanged FINAL)

public:
    explicit TrackTableProxy(QAbstractItemModel *source, QObject *parent = nullptr);

    QString sortKey() const { return m_sortKey; }
    void setSortKey(const QString &key);
    bool descending() const { return m_descending; }
    void setDescending(bool on);
    int classFilter() const { return m_classFilter; }
    void setClassFilter(int cls); // -1: all
    int count() const { return rowCount(); }
    int total() const { return sourceModel() ? sourceModel()->rowCount() : 0; }

signals:
    void sortChanged();
    void classFilterChanged();
    void countChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    void applySort();

    QString m_sortKey = QStringLiteral("trackId");
    bool m_descending = false;
    int m_classFilter = -1;
};

// The event log: severity toggles and a text search.
class EventFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by Scene")

    Q_PROPERTY(int severities READ severities WRITE setSeverities NOTIFY filterChanged FINAL) // bit per level
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY filterChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    Q_PROPERTY(int total READ total NOTIFY countChanged FINAL)

public:
    explicit EventFilterProxy(EventLogModel *source, QObject *parent = nullptr);

    int severities() const { return m_severities; }
    void setSeverities(int mask);
    QString text() const { return m_text; }
    void setText(const QString &text);
    int count() const { return rowCount(); }
    int total() const { return sourceModel() ? sourceModel()->rowCount() : 0; }

signals:
    void filterChanged();
    void countChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    int m_severities = 0b111;
    QString m_text;
};

// The event log's filter as an index of matching rows. A filter change is one reset (the
// proxy above reports one removal per gap: 1,251 signals for "late"); new events are one
// insert per batch, trims one removal at the front.
class EventFilterModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by Scene")

    Q_PROPERTY(int severities READ severities WRITE setSeverities NOTIFY filterChanged FINAL)
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY filterChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    Q_PROPERTY(int total READ total NOTIFY countChanged FINAL)

public:
    explicit EventFilterModel(EventLogModel *source, QObject *parent = nullptr);

    int severities() const { return m_severities; }
    void setSeverities(int mask);
    QString text() const { return m_text; }
    void setText(const QString &text);
    int count() const { return int(m_rows.size()); }
    int total() const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void filterChanged();
    void countChanged();

private:
    bool accepts(int sourceRow) const;
    void rebuild();

    EventLogModel *m_source;
    // Each matching event's position counted from the first event ever kept; source row =
    // position - m_trimmed. Survives trims at the front without renumbering.
    std::deque<qint64> m_rows;
    qint64 m_trimmed = 0;
    int m_severities = 0b111;
    QString m_text;
};
