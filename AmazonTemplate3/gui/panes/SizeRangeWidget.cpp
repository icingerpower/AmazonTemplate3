#include "SizeRangeWidget.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>

#include "sizecategories/AbstractSizeCategory.h"

SizeRangeWidget::SizeRangeWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_radioNumbers = new QRadioButton(tr("Numbers"), this);
    m_radioLetters = new QRadioButton(tr("Letters"), this);
    m_radioHeight  = new QRadioButton(tr("Height"),  this);
    m_radioNumbers->setChecked(true);

    m_numFrom = new QComboBox(this);
    m_numTo   = new QComboBox(this);
    m_letFrom = new QComboBox(this);
    m_letTo   = new QComboBox(this);
    m_hgtFrom = new QComboBox(this);
    m_hgtTo   = new QComboBox(this);

    auto makeSep = [this]() {
        auto *line = new QFrame(this);
        line->setFrameShape(QFrame::VLine);
        line->setFrameShadow(QFrame::Sunken);
        return line;
    };

    layout->addWidget(m_radioNumbers);
    layout->addWidget(m_numFrom);
    layout->addWidget(new QLabel(tr("to"), this));
    layout->addWidget(m_numTo);

    layout->addWidget(makeSep());

    layout->addWidget(m_radioLetters);
    layout->addWidget(m_letFrom);
    layout->addWidget(new QLabel(tr("to"), this));
    layout->addWidget(m_letTo);

    layout->addWidget(makeSep());

    layout->addWidget(m_radioHeight);
    layout->addWidget(m_hgtFrom);
    layout->addWidget(new QLabel(tr("to"), this));
    layout->addWidget(m_hgtTo);

    // Wire signals
    const auto comboChanged = QOverload<int>::of(&QComboBox::currentIndexChanged);
    connect(m_numFrom, comboChanged, this, &SizeRangeWidget::changed);
    connect(m_numTo,   comboChanged, this, &SizeRangeWidget::changed);
    connect(m_letFrom, comboChanged, this, &SizeRangeWidget::changed);
    connect(m_letTo,   comboChanged, this, &SizeRangeWidget::changed);
    connect(m_hgtFrom, comboChanged, this, &SizeRangeWidget::changed);
    connect(m_hgtTo,   comboChanged, this, &SizeRangeWidget::changed);

    connect(m_radioNumbers, &QRadioButton::toggled, this, &SizeRangeWidget::onModeToggled);
    connect(m_radioLetters, &QRadioButton::toggled, this, &SizeRangeWidget::onModeToggled);
    connect(m_radioHeight,  &QRadioButton::toggled, this, &SizeRangeWidget::onModeToggled);

    updateControlStates();
}

void SizeRangeWidget::setCategory(const AbstractSizeCategory *cat)
{
    m_cat = cat;

    // Block signals while repopulating to avoid spurious "changed" emissions.
    const QSignalBlocker b1(m_numFrom);
    const QSignalBlocker b2(m_numTo);
    const QSignalBlocker b3(m_letFrom);
    const QSignalBlocker b4(m_letTo);
    const QSignalBlocker b5(m_hgtFrom);
    const QSignalBlocker b6(m_hgtTo);
    const QSignalBlocker br1(m_radioNumbers);
    const QSignalBlocker br2(m_radioLetters);
    const QSignalBlocker br3(m_radioHeight);

    m_numFrom->clear();
    m_numTo->clear();
    m_letFrom->clear();
    m_letTo->clear();
    m_hgtFrom->clear();
    m_hgtTo->clear();

    if (!cat) {
        updateControlStates();
        return;
    }

    const bool isHeightBased = cat->referenceKey() == QStringLiteral("HEIGHT");
    const bool hasLetters    = !cat->letterSizes().isEmpty();

    const QStringList keys = cat->referenceKeys();
    m_numFrom->addItems(keys);
    m_numTo->addItems(keys);
    m_numFrom->setCurrentIndex(-1);
    m_numTo->setCurrentIndex(-1);

    if (hasLetters) {
        const QStringList letters = cat->letterSizes();
        m_letFrom->addItems(letters);
        m_letTo->addItems(letters);
    }
    m_letFrom->setCurrentIndex(-1);
    m_letTo->setCurrentIndex(-1);

    if (isHeightBased) {
        m_hgtFrom->addItems(keys);
        m_hgtTo->addItems(keys);
    }
    m_hgtFrom->setCurrentIndex(-1);
    m_hgtTo->setCurrentIndex(-1);

    m_radioNumbers->setEnabled(!isHeightBased);
    m_radioLetters->setEnabled(hasLetters);
    m_radioHeight ->setEnabled(isHeightBased);

    if (isHeightBased) {
        m_radioHeight->setChecked(true);
    } else if (m_radioHeight->isChecked()) {
        m_radioNumbers->setChecked(true);
    }
    if (!hasLetters && m_radioLetters->isChecked())
        m_radioNumbers->setChecked(true);

    updateControlStates();
}

QString SizeRangeWidget::mode() const
{
    if (m_radioLetters->isChecked()) return QStringLiteral("letters");
    if (m_radioHeight->isChecked())  return QStringLiteral("height");
    return QStringLiteral("numbers");
}

QString SizeRangeWidget::from() const
{
    const QString m = mode();
    if (m == QLatin1String("letters")) return m_letFrom->currentText();
    if (m == QLatin1String("height"))  return m_hgtFrom->currentText();
    return m_numFrom->currentText();
}

QString SizeRangeWidget::to() const
{
    const QString m = mode();
    if (m == QLatin1String("letters")) return m_letTo->currentText();
    if (m == QLatin1String("height"))  return m_hgtTo->currentText();
    return m_numTo->currentText();
}

bool SizeRangeWidget::isRangeSelected() const
{
    const QString m = mode();
    if (m == QLatin1String("letters"))
        return m_letFrom->currentIndex() >= 0 && m_letTo->currentIndex() >= 0;
    if (m == QLatin1String("height"))
        return m_hgtFrom->currentIndex() >= 0 && m_hgtTo->currentIndex() >= 0;
    return m_numFrom->currentIndex() >= 0 && m_numTo->currentIndex() >= 0;
}

void SizeRangeWidget::setMode(const QString &mode)
{
    if (mode == QLatin1String("letters"))      m_radioLetters->setChecked(true);
    else if (mode == QLatin1String("height"))  m_radioHeight->setChecked(true);
    else                                        m_radioNumbers->setChecked(true);
}

void SizeRangeWidget::setFrom(const QString &val)
{
    const QString m = mode();
    if (m == QLatin1String("letters"))      m_letFrom->setCurrentText(val);
    else if (m == QLatin1String("height"))  m_hgtFrom->setCurrentText(val);
    else                                     m_numFrom->setCurrentText(val);
}

void SizeRangeWidget::setTo(const QString &val)
{
    const QString m = mode();
    if (m == QLatin1String("letters"))      m_letTo->setCurrentText(val);
    else if (m == QLatin1String("height"))  m_hgtTo->setCurrentText(val);
    else                                     m_numTo->setCurrentText(val);
}

void SizeRangeWidget::guessRange(const QStringList &rawSizes)
{
    if (!m_cat)
        return;
    const auto [minKey, maxKey] = m_cat->guessRange(rawSizes);
    if (!minKey.isEmpty()) {
        m_numFrom->setCurrentText(minKey);
        m_numTo->setCurrentText(maxKey.isEmpty() ? minKey : maxKey);
    }
}

void SizeRangeWidget::onModeToggled()
{
    updateControlStates();
    emit changed();
}

void SizeRangeWidget::updateControlStates()
{
    const QString m = mode();
    const bool useNumbers = (m == QLatin1String("numbers"));
    const bool useLetters = (m == QLatin1String("letters"));
    const bool useHeight  = (m == QLatin1String("height"));

    m_numFrom->setEnabled(useNumbers);
    m_numTo->setEnabled(useNumbers);
    m_letFrom->setEnabled(useLetters);
    m_letTo->setEnabled(useLetters);
    m_hgtFrom->setEnabled(useHeight);
    m_hgtTo->setEnabled(useHeight);
}
