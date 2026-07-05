#include "RectangleCategory.h"

#include <QObject>

QString RectangleCategory::displayName() const
{
    return QObject::tr("Rectangle – fixed dimensions");
}
