#ifndef DIALOGATTRIBUTES_H
#define DIALOGATTRIBUTES_H

#include <QDialog>

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

public slots:
    void missingPossibleAdd();
    void missingPossibleRemove();
    void replaceAdd();
    void replaceRemove();

private:
    Ui::DialogAttributes *ui;
    TemplateFiller *m_templateFiller;
    void _connectSlots();
};

#endif // DIALOGATTRIBUTES_H
