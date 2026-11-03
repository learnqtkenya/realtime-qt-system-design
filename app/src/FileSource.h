#pragma once

#include "Recording.h"
#include "Source.h"

// Plays a recording. Used from one thread (the presenter).
class FileSource : public Source
{
public:
    bool open(const QString &path, QString *error);
    const rec::Reader &reader() const { return m_reader; }
    rec::Reader &reader() { return m_reader; }

    std::vector<StreamId> streams() const override;
    qint64 startNs() const override { return m_reader.startNs(); }
    qint64 endNs() const override { return m_reader.endNs(); }
    std::optional<Sample> latestIn(StreamId id, qint64 from, qint64 to) override;
    void allIn(StreamId id, qint64 from, qint64 to, std::vector<Sample> &out) override;
    std::optional<qint64> nextAfter(StreamId id, qint64 after) override;
    std::optional<qint64> prevBefore(StreamId id, qint64 before) override;

private:
    const std::vector<rec::RecordRef> *refs(StreamId id) const;
    std::optional<Sample> decode(const rec::RecordRef &ref);

    rec::Reader m_reader;
};
