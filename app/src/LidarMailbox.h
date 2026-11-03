#pragma once

#include "qp/SimTypes.h"

#include <QObject>

#include <mutex>
#include <vector>

// Latest frame per sensor. Any thread may submit or read.
class LidarMailbox : public QObject
{
    Q_OBJECT

public:
    struct Entry
    {
        qp::SensorFrame frame;
        quint64 serial = 0; // 0 = nothing yet
    };

    explicit LidarMailbox(int sensors, QObject *parent = nullptr);

    int sensorCount() const { return int(m_entries.size()); }
    void submit(const qp::SensorFrame &frame);
    Entry latest(int sensor) const;

signals:
    void changed(int sensorId);

private:
    mutable std::mutex m_mutex;
    std::vector<Entry> m_entries;
    quint64 m_serial = 0;
};
