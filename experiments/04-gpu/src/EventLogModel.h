#pragma once

#include "qp/SimTypes.h"

#include <QAbstractListModel>

#include <vector>

class EventLogModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles { SeverityRole = Qt::UserRole + 1, SourceRole, MessageRole, SeqRole };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void appendBatch(const qp::EventBatch &batch);

private:
    std::vector<qp::EventRecord> m_events;
};
