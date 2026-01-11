#ifndef DIALOGATTRIBUTES_H
#define DIALOGATTRIBUTES_H

#include <QDialog>

#include <QCoro/QCoroCore>

namespace Ui {
class DialogAttributes;
}

class TemplateFiller;

class DialogAttributes : public QDialog
{
    Q_OBJECT

public:
    explicit DialogAttributes(TemplateFiller *templateFiller, QWidget *parent = nullptr);
    ~DialogAttributes();
    static QCoro::Task<bool> editAttributes(TemplateFiller *templateFiller, const QString &title, const QString &message);

public slots:
    void missingPossibleAdd();
    void missingPossibleRemove();
    void replaceAdd();
    void replaceRemove();
    void equivalentRemove();
    void flagsAdd();

private:
    Ui::DialogAttributes *ui;
    TemplateFiller *m_templateFiller;
    void _connectSlots();
};

#endif // DIALOGATTRIBUTES_H
