#include "ExceptionTemplate.h"

void ExceptionTemplate::raise() const
{
    throw *this;
}

ExceptionTemplate *ExceptionTemplate::clone() const
{
    return new ExceptionTemplate(*this);
}

void ExceptionTemplate::setInfos(const QString &title, const QString &error)
{
    m_title = title;
    m_error = error;
}

const QString &ExceptionTemplate::title() const
{
    return m_title;
}

const QString &ExceptionTemplate::error() const
{
    return m_error;
}
