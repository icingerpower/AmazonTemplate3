#include "TemplateExceptions.h"

void TemplateExceptions::raise() const
{
    throw *this;
}

TemplateExceptions *TemplateExceptions::clone() const
{
    return new TemplateExceptions(*this);
}

void TemplateExceptions::setInfos(const QString &title, const QString &error)
{
    m_title = title;
    m_error = error;
}

const QString &TemplateExceptions::title() const
{
    return m_title;
}

const QString &TemplateExceptions::error() const
{
    return m_error;
}
