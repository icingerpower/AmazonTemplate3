#ifndef SIZERANGEWIDGET_H
#define SIZERANGEWIDGET_H

#include <QWidget>
#include <QStringList>

class AbstractSizeCategory;
class QRadioButton;
class QComboBox;

class SizeRangeWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SizeRangeWidget(QWidget *parent = nullptr);

    void    setCategory(const AbstractSizeCategory *cat);

    QString mode()           const;   // "numbers" / "letters" / "height" / "one_size"
    QString from()           const;
    QString to()             const;
    bool    isRangeSelected() const;  // both from and to have a valid selection

    void    setMode(const QString &mode);
    void    setFrom(const QString &val);
    void    setTo(const QString &val);

    // Applies cat->guessRange(rawSizes) to the numbers combos
    void    guessRange(const QStringList &rawSizes);

signals:
    void changed();

private slots:
    void onModeToggled();

private:
    QRadioButton *m_radioNumbers;
    QRadioButton *m_radioLetters;
    QRadioButton *m_radioHeight;
    QRadioButton *m_radioOneSize;
    QComboBox    *m_numFrom, *m_numTo;
    QComboBox    *m_letFrom, *m_letTo;
    QComboBox    *m_hgtFrom, *m_hgtTo;
    const AbstractSizeCategory *m_cat = nullptr;

    void updateControlStates();
};

#endif
