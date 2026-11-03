#include "EventLogModel.h"

#include "qp/StageStats.h"

int EventLogModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_events.size());
}

QVariant EventLogModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= int(m_events.size()))
        return {};
    const qp::EventRecord &e = m_events[size_t(index.row())];
    switch (role) {
    case SeverityRole: return int(e.severity);
    case SourceRole: return e.source;
    case MessageRole: return e.text;
    case SeqRole: return qulonglong(e.seq);
    default: return {};
    }
}

QHash<int, QByteArray> EventLogModel::roleNames() const
{
    return {
        {SeverityRole, "severity"},
        {SourceRole, "source"},
        {MessageRole, "message"},
        {SeqRole, "seq"},
    };
}

void EventLogModel::appendBatch(const qp::EventBatch &batch)
{
    const int64_t t0 = qp::nowNs();
    beginResetModel();
    m_events.insert(m_events.end(), batch.begin(), batch.end());
    endResetModel();
    qp::StageStats::instance().add(QStringLiteral("event_reset_ms"), double(qp::nowNs() - t0) / 1e6);
}
