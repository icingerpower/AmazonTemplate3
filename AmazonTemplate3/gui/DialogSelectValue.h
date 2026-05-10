#pragma once

#include <QDialog>
#include <QSet>
#include <QCoro/QCoroCore>

class QListWidget;
class QLineEdit;
class QPushButton;
class TemplateFiller;

class DialogSelectValue : public QDialog
{
    Q_OBJECT
public:
    explicit DialogSelectValue(
        const QString &title,
        const QString &description,
        const QString &aiPrompt,
        const QString &imagePath,
        const QStringList &possibleValues,
        QWidget *parent = nullptr);

    QString selectedValue() const;

    static QCoro::Task<QString> selectValue(
        TemplateFiller *templateFiller,
        const QString &title,
        const QString &description,
        const QString &aiPrompt,
        const QString &imagePath,
        const QSet<QString> &possibleValues);

private:
    void applyFilter(const QString &text);

    QListWidget *m_list;
    QPushButton *m_okButton;
    QStringList m_allValues;
};
