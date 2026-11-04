#pragma once

#include "qp/SimTypes.h"

#include <QAbstractListModel>

#include <deque>

class EventLogModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles { SeverityRole = Qt::UserRole + 1, SourceRole, MessageRole, SeqRole };
    // Reset: keep everything, reset on every batch. Diff: insert the batch at the end and remove
    // what falls past the cap from the start.
    enum class Updates { Reset, Diff };

    using QAbstractListModel::QAbstractListModel;

    void setUpdates(Updates updates, int cap) { m_updates = updates; m_cap = cap; }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // For filters that would otherwise pay for a QVariant per role per row.
    const qp::EventRecord &at(int row) const { return m_events[size_t(row)]; }

    void appendBatch(const qp::EventBatch &batch);
    void replaceAll(const qp::EventBatch &batch);

private:
    std::deque<qp::EventRecord> m_events;
    Updates m_updates = Updates::Reset;
    int m_cap = 10'000;
};
