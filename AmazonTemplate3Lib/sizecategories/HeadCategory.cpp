#include "HeadCategory.h"

#include <QObject>

QString HeadCategory::displayName() const
{
    return QObject::tr("Head – no sizing");
}
