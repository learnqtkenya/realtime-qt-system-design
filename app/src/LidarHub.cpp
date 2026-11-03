#include "LidarHub.h"

#include "LidarMailbox.h"
#include "LidarPipeline.h"

LidarHub::LidarHub(LidarPipeline *pipeline, LidarMailbox *mailbox, QObject *parent)
    : QObject(parent), m_pipeline(pipeline), m_mailbox(mailbox)
{}

void LidarHub::submit(const qp::SensorFrame &frame)
{
    switch (m_route.load()) {
    case Route::None: break;
    case Route::Canvas: emit canvasFrame(frame); break;
    case Route::Store: emit storeFrame(frame); break;
    case Route::Pipeline: m_pipeline->submit(frame); break;
    case Route::Mailbox: m_mailbox->submit(frame); break;
    }
}
