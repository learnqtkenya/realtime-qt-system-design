#include "Proxies.h"

#include "EventLogModel.h"
#include "TrackListModel.h"
#include "qp/StageStats.h"


namespace {

template <typename P> void emitCountOnChanges(P *proxy)
{
    for (auto sig : {&QAbstractItemModel::rowsInserted, &QAbstractItemModel::rowsRemoved})
        QObject::connect(proxy, sig, proxy, &P::countChanged);
    QObject::connect(proxy, &QAbstractItemModel::modelReset, proxy, &P::countChanged);
    QObject::connect(proxy, &QAbstractItemModel::layoutChanged, proxy, &P::countChanged);
}

} // namespace

TrackTableProxy::TrackTableProxy(QAbstractItemModel *source, QObject *parent) : QSortFilterProxyModel(parent)
{
    setSourceModel(source);
    setDynamicSortFilter(true);
    emitCountOnChanges(this);
    for (auto sig : {&QAbstractItemModel::rowsInserted, &QAbstractItemModel::rowsRemoved})
        connect(source, sig, this, &TrackTableProxy::countChanged);
    connect(source, &QAbstractItemModel::modelReset, this, &TrackTableProxy::countChanged);
    applySort();
}

void TrackTableProxy::applySort()
{
    const QHash<int, QByteArray> roles = sourceModel()->roleNames();
    setSortRole(roles.key(m_sortKey.toUtf8(), TrackListModel::TrackIdRole));
    sort(0, m_descending ? Qt::DescendingOrder : Qt::AscendingOrder);
}

void TrackTableProxy::setSortKey(const QString &key)
{
    if (key == m_sortKey)
        return;
    m_sortKey = key;
    applySort();
    emit sortChanged();
}

void TrackTableProxy::setDescending(bool on)
{
    if (on == m_descending)
        return;
    m_descending = on;
    applySort();
    emit sortChanged();
}

void TrackTableProxy::setClassFilter(int cls)
{
    if (cls == m_classFilter)
        return;
    beginFilterChange();
    m_classFilter = cls;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
    emit classFilterChanged();
}

bool TrackTableProxy::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    if (m_classFilter < 0)
        return true;
    return sourceModel()->index(sourceRow, 0, sourceParent).data(TrackListModel::ClassRole).toInt() == m_classFilter;
}

EventFilterProxy::EventFilterProxy(EventLogModel *source, QObject *parent) : QSortFilterProxyModel(parent)
{
    setSourceModel(source);
    setDynamicSortFilter(true);
    emitCountOnChanges(this);
    for (auto sig : {&QAbstractItemModel::rowsInserted, &QAbstractItemModel::rowsRemoved})
        connect(source, sig, this, &EventFilterProxy::countChanged);
    connect(source, &QAbstractItemModel::modelReset, this, &EventFilterProxy::countChanged);
}

void EventFilterProxy::setSeverities(int mask)
{
    if (mask == m_severities)
        return;
    const int64_t t0 = qp::nowNs();
    beginFilterChange();
    m_severities = mask;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
    qp::StageStats::instance().add(QStringLiteral("event_filter_ms"), double(qp::nowNs() - t0) / 1e6);
    emit filterChanged();
}

void EventFilterProxy::setText(const QString &text)
{
    if (text == m_text)
        return;
    const int64_t t0 = qp::nowNs();
    beginFilterChange();
    m_text = text;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
    qp::StageStats::instance().add(QStringLiteral("event_filter_ms"), double(qp::nowNs() - t0) / 1e6);
    emit filterChanged();
}

bool EventFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    Q_UNUSED(sourceParent)
    const qp::EventRecord &e = static_cast<const EventLogModel *>(sourceModel())->at(sourceRow);
    if (!(m_severities & (1 << e.severity)))
        return false;
    return m_text.isEmpty() || e.text.contains(m_text, Qt::CaseInsensitive)
           || e.source.contains(m_text, Qt::CaseInsensitive);
}

EventFilterModel::EventFilterModel(EventLogModel *source, QObject *parent)
    : QAbstractListModel(parent), m_source(source)
{
    connect(source, &QAbstractItemModel::rowsInserted, this, [this](const QModelIndex &, int first, int last) {
        std::vector<qint64> added;
        for (int r = first; r <= last; ++r)
            if (accepts(r))
                added.push_back(m_trimmed + r);
        if (!added.empty()) {
            const int at = int(m_rows.size());
            beginInsertRows({}, at, at + int(added.size()) - 1);
            m_rows.insert(m_rows.end(), added.begin(), added.end());
            endInsertRows();
        }
        emit countChanged();
    });
    // The source only removes from the front (its cap).
    connect(source, &QAbstractItemModel::rowsRemoved, this, [this](const QModelIndex &, int first, int last) {
        Q_ASSERT(first == 0);
        m_trimmed += last - first + 1;
        int gone = 0;
        while (gone < int(m_rows.size()) && m_rows[size_t(gone)] < m_trimmed)
            ++gone;
        if (gone) {
            beginRemoveRows({}, 0, gone - 1);
            m_rows.erase(m_rows.begin(), m_rows.begin() + gone);
            endRemoveRows();
        }
        emit countChanged();
    });
    connect(source, &QAbstractItemModel::modelReset, this, [this] {
        beginResetModel();
        rebuild();
        endResetModel();
        emit countChanged();
    });
    rebuild();
}

int EventFilterModel::total() const
{
    return m_source->rowCount();
}

bool EventFilterModel::accepts(int sourceRow) const
{
    const qp::EventRecord &e = m_source->at(sourceRow);
    if (!(m_severities & (1 << e.severity)))
        return false;
    return m_text.isEmpty() || e.text.contains(m_text, Qt::CaseInsensitive)
           || e.source.contains(m_text, Qt::CaseInsensitive);
}

void EventFilterModel::rebuild()
{
    m_rows.clear();
    m_trimmed = 0;
    for (int r = 0, n = m_source->rowCount(); r < n; ++r)
        if (accepts(r))
            m_rows.push_back(r);
}

void EventFilterModel::setSeverities(int mask)
{
    if (mask == m_severities)
        return;
    const int64_t t0 = qp::nowNs();
    beginResetModel();
    m_severities = mask;
    rebuild();
    endResetModel();
    qp::StageStats::instance().add(QStringLiteral("event_filter_ms"), double(qp::nowNs() - t0) / 1e6);
    emit filterChanged();
    emit countChanged();
}

void EventFilterModel::setText(const QString &text)
{
    if (text == m_text)
        return;
    const int64_t t0 = qp::nowNs();
    beginResetModel();
    m_text = text;
    rebuild();
    endResetModel();
    qp::StageStats::instance().add(QStringLiteral("event_filter_ms"), double(qp::nowNs() - t0) / 1e6);
    emit filterChanged();
    emit countChanged();
}

int EventFilterModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant EventFilterModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= int(m_rows.size()))
        return {};
    return m_source->data(m_source->index(int(m_rows[size_t(index.row())] - m_trimmed)), role);
}

QHash<int, QByteArray> EventFilterModel::roleNames() const
{
    return m_source->roleNames();
}
