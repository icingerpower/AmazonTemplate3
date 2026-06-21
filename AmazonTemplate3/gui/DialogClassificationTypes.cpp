#include "DialogClassificationTypes.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

DialogClassificationTypes::DialogClassificationTypes(
    const QList<UnknownClassification> &unknowns,
    const QStringList &knownTypes,
    QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Assign product types to unknown classifications"));
    resize(900, 400);

    auto *layout = new QVBoxLayout(this);

    auto *label = new QLabel(
        tr("The Amazon Catalog API returned these browse classification IDs without a product type.\n"
           "Assign the correct Amazon product type (e.g. SHIRT, SWIMWEAR, HEALTH_PERSONAL_CARE) "
           "to each one so the upload can proceed. The mapping is saved and reused automatically."),
        this);
    label->setWordWrap(true);
    layout->addWidget(label);

    m_table = new QTableWidget(static_cast<int>(unknowns.size()), 4, this);
    m_table->setHorizontalHeaderLabels(
        {tr("Classification ID"), tr("Category"), tr("Example product"), tr("Product type")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->verticalHeader()->hide();

    for (int r = 0; r < unknowns.size(); ++r) {
        const UnknownClassification &u = unknowns.at(r);
        m_table->setItem(r, 0, new QTableWidgetItem(u.classificationId));
        m_table->setItem(r, 1, new QTableWidgetItem(u.displayName));
        m_table->setItem(r, 2, new QTableWidgetItem(
            u.exampleAsin.isEmpty()
                ? u.exampleTitle
                : QStringLiteral("%1 — %2").arg(u.exampleAsin, u.exampleTitle)));

        auto *combo = new QComboBox(this);
        combo->setEditable(true);
        combo->setInsertPolicy(QComboBox::NoInsert);
        combo->addItem(QString{}); // empty first entry
        combo->addItems(knownTypes);
        m_table->setCellWidget(r, 3, combo);
    }

    layout->addWidget(m_table);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QHash<QString, QString> DialogClassificationTypes::result() const
{
    QHash<QString, QString> out;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        const QTableWidgetItem *idItem = m_table->item(r, 0);
        const auto *combo = qobject_cast<QComboBox *>(m_table->cellWidget(r, 3));
        if (!idItem || !combo) continue;
        const QString pt = combo->currentText().trimmed().toUpper();
        if (!pt.isEmpty())
            out.insert(idItem->text(), pt);
    }
    return out;
}
