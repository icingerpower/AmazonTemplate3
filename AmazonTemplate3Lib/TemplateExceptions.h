#ifndef TEMPLATEEXCEPTIONS_H
#define TEMPLATEEXCEPTIONS_H

#include <QException>

class TemplateExceptions : public QException
{
public:
    void raise() const override;
    TemplateExceptions *clone() const override;
    void setInfos(const QString &title, const QString &error);

    const QString &title() const;
    const QString &error() const;

private:
    QString m_title;
    QString m_error;
};


#endif // TEMPLATEEXCEPTIONS_H
