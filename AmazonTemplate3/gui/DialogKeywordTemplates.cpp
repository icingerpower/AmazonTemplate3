#include "DialogKeywordTemplates.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QUuid>
#include <QVBoxLayout>

#include "../../common/workingdirectory/WorkingDirectoryManager.h"

static const QString kSettingsKey = QStringLiteral("TemuKeywordTemplatesJson");

// --- Persistence -----------------------------------------------------------

QList<KeywordTemplate> DialogKeywordTemplates::load()
{
    QList<KeywordTemplate> out;
    const QByteArray json = WorkingDirectoryManager::instance()->settings()
        ->value(kSettingsKey).toByteArray();
    for (const QJsonValue &tv : QJsonDocument::fromJson(json).array()) {
        const QJsonObject o = tv.toObject();
        KeywordTemplate t;
        t.id   = o.value(QStringLiteral("id")).toString();
        t.name = o.value(QStringLiteral("name")).toString();
        const QJsonObject c = o.value(QStringLiteral("countries")).toObject();
        for (auto it = c.begin(); it != c.end(); ++it) {
            QStringList kws;
            for (const QJsonValue &kv : it.value().toArray())
                kws << kv.toString();
            t.byCountry.insert(it.key(), kws);
        }
        out.append(t);
    }
    return out;
}

void DialogKeywordTemplates::save(const QList<KeywordTemplate> &templates)
{
    QJsonArray arr;
    for (const KeywordTemplate &t : templates) {
        QJsonObject countries;
        for (auto it = t.byCountry.begin(); it != t.byCountry.end(); ++it) {
            QJsonArray kws;
            for (const QString &k : it.value())
                kws.append(k);
            countries.insert(it.key(), kws);
        }
        QJsonObject o;
        o.insert(QStringLiteral("id"), t.id);
        o.insert(QStringLiteral("name"), t.name);
        o.insert(QStringLiteral("countries"), countries);
        arr.append(o);
    }
    WorkingDirectoryManager::instance()->settings()->setValue(
        kSettingsKey, QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QStringList DialogKeywordTemplates::keywordsFor(const QString &templateId, const QString &country)
{
    if (templateId.isEmpty())
        return {};
    for (const KeywordTemplate &t : load())
        if (t.id == templateId)
            return t.byCountry.value(country.toUpper());
    return {};
}

// --- Editor ----------------------------------------------------------------

DialogKeywordTemplates::DialogKeywordTemplates(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Title keyword templates"));
    resize(640, 460);
    m_templates = load();

    m_list = new QListWidget(this);
    auto *addTplBtn = new QPushButton(tr("Add"), this);
    auto *delTplBtn = new QPushButton(tr("Remove"), this);
    auto *tplBtns = new QHBoxLayout;
    tplBtns->addWidget(addTplBtn);
    tplBtns->addWidget(delTplBtn);
    auto *leftCol = new QVBoxLayout;
    leftCol->addWidget(new QLabel(tr("Templates (double-click to rename):"), this));
    leftCol->addWidget(m_list, 1);
    leftCol->addLayout(tplBtns);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({tr("Country / keyword")});
    auto *addCountryBtn = new QPushButton(tr("Add country"), this);
    auto *addKwBtn      = new QPushButton(tr("Add keyword"), this);
    auto *addKwsBtn     = new QPushButton(tr("Add keywords…"), this);
    addKwsBtn->setToolTip(tr("Enter several keywords at once, one per line."));
    auto *delItemBtn    = new QPushButton(tr("Remove"), this);
    auto *treeBtns = new QHBoxLayout;
    treeBtns->addWidget(addCountryBtn);
    treeBtns->addWidget(addKwBtn);
    treeBtns->addWidget(addKwsBtn);
    treeBtns->addWidget(delItemBtn);
    auto *rightCol = new QVBoxLayout;
    rightCol->addWidget(new QLabel(tr("Countries → keywords to force into the title:"), this));
    rightCol->addWidget(m_tree, 1);
    rightCol->addLayout(treeBtns);

    auto *cols = new QHBoxLayout;
    cols->addLayout(leftCol, 1);
    cols->addLayout(rightCol, 2);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *lay = new QVBoxLayout(this);
    lay->addLayout(cols, 1);
    lay->addWidget(box);

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        _commitCurrentTree();
        _showTemplate(row);
    });
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem *it) {
        const int row = m_list->row(it);
        if (row >= 0 && row < m_templates.size())
            m_templates[row].name = it->text();
    });
    connect(addTplBtn, &QPushButton::clicked, this, &DialogKeywordTemplates::_addTemplate);
    connect(delTplBtn, &QPushButton::clicked, this, &DialogKeywordTemplates::_removeTemplate);
    connect(addCountryBtn, &QPushButton::clicked, this, &DialogKeywordTemplates::_addCountry);
    connect(addKwBtn, &QPushButton::clicked, this, &DialogKeywordTemplates::_addKeyword);
    connect(addKwsBtn, &QPushButton::clicked, this, &DialogKeywordTemplates::_addKeywords);
    connect(delItemBtn, &QPushButton::clicked, this, &DialogKeywordTemplates::_removeTreeItem);

    _reloadTemplateList();
    if (!m_templates.isEmpty())
        m_list->setCurrentRow(0);
}

void DialogKeywordTemplates::_reloadTemplateList()
{
    QSignalBlocker b(m_list);
    m_list->clear();
    for (const KeywordTemplate &t : m_templates) {
        auto *it = new QListWidgetItem(t.name, m_list);
        it->setFlags(it->flags() | Qt::ItemIsEditable);
    }
}

void DialogKeywordTemplates::_showTemplate(int index)
{
    m_current = index;
    m_tree->clear();
    if (index < 0 || index >= m_templates.size())
        return;
    const auto &t = m_templates[index];
    for (auto it = t.byCountry.begin(); it != t.byCountry.end(); ++it) {
        auto *countryItem = new QTreeWidgetItem(m_tree, {it.key()});
        countryItem->setExpanded(true);
        for (const QString &kw : it.value()) {
            auto *kwItem = new QTreeWidgetItem(countryItem, {kw});
            kwItem->setFlags(kwItem->flags() | Qt::ItemIsEditable);
        }
    }
}

void DialogKeywordTemplates::_commitCurrentTree()
{
    if (m_current < 0 || m_current >= m_templates.size())
        return;
    QMap<QString, QStringList> byCountry;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *c = m_tree->topLevelItem(i);
        const QString country = c->text(0).trimmed().toUpper();
        if (country.isEmpty())
            continue;
        QStringList kws;
        for (int j = 0; j < c->childCount(); ++j) {
            const QString kw = c->child(j)->text(0).trimmed();
            if (!kw.isEmpty())
                kws << kw;
        }
        byCountry.insert(country, kws);
    }
    m_templates[m_current].byCountry = byCountry;
}

void DialogKeywordTemplates::_addTemplate()
{
    KeywordTemplate t;
    t.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    t.name = tr("New template");
    m_templates.append(t);
    _reloadTemplateList();
    m_list->setCurrentRow(m_templates.size() - 1);
    m_list->editItem(m_list->currentItem());
}

void DialogKeywordTemplates::_removeTemplate()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_templates.size())
        return;
    m_templates.removeAt(row);
    m_current = -1;
    _reloadTemplateList();
    if (!m_templates.isEmpty())
        m_list->setCurrentRow(qMin(row, m_templates.size() - 1));
    else
        m_tree->clear();
}

void DialogKeywordTemplates::_addCountry()
{
    if (m_current < 0) {
        QMessageBox::information(this, tr("Add country"), tr("Select a template first."));
        return;
    }
    // Countries already present in the tree — offer only the ones not yet added.
    QStringList existing;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        existing << m_tree->topLevelItem(i)->text(0);

    bool ok = false;
    QString code;
    if (!m_availableCountries.isEmpty()) {
        // Pick from the countries selected for this product's Temu stores.
        QStringList choices = m_availableCountries;
        choices.removeIf([&](const QString &c) {
            return existing.contains(c, Qt::CaseInsensitive);
        });
        if (choices.isEmpty()) {
            QMessageBox::information(this, tr("Add country"),
                tr("All selected countries are already added."));
            return;
        }
        code = QInputDialog::getItem(this, tr("Add country"),
            tr("Country (from the stores selected for this product):"),
            choices, 0, false, &ok).trimmed().toUpper();
    } else {
        // Standalone use (no store context): fall back to free-text entry.
        code = QInputDialog::getText(this, tr("Add country"),
            tr("Country code (e.g. FR, DE, IT):"), QLineEdit::Normal, QString{}, &ok)
            .trimmed().toUpper();
    }
    if (!ok || code.isEmpty())
        return;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        if (m_tree->topLevelItem(i)->text(0) == code) {
            m_tree->setCurrentItem(m_tree->topLevelItem(i));
            return;
        }
    auto *item = new QTreeWidgetItem(m_tree, {code});
    item->setExpanded(true);
    m_tree->setCurrentItem(item);
}

void DialogKeywordTemplates::_addKeyword()
{
    QTreeWidgetItem *cur = m_tree->currentItem();
    if (!cur) {
        QMessageBox::information(this, tr("Add keyword"), tr("Select a country first."));
        return;
    }
    QTreeWidgetItem *country = cur->parent() ? cur->parent() : cur;
    auto *kw = new QTreeWidgetItem(country, {tr("keyword")});
    kw->setFlags(kw->flags() | Qt::ItemIsEditable);
    country->setExpanded(true);
    m_tree->setCurrentItem(kw);
    m_tree->editItem(kw, 0);
}

void DialogKeywordTemplates::_addKeywords()
{
    QTreeWidgetItem *cur = m_tree->currentItem();
    if (!cur) {
        QMessageBox::information(this, tr("Add keywords"), tr("Select a country first."));
        return;
    }
    QTreeWidgetItem *country = cur->parent() ? cur->parent() : cur;

    bool ok = false;
    const QString text = QInputDialog::getMultiLineText(this, tr("Add keywords"),
        tr("One keyword per line:"), QString{}, &ok);
    if (!ok)
        return;

    QTreeWidgetItem *lastAdded = nullptr;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QString kw = line.trimmed();
        if (kw.isEmpty())
            continue;
        auto *kwItem = new QTreeWidgetItem(country, {kw});
        kwItem->setFlags(kwItem->flags() | Qt::ItemIsEditable);
        lastAdded = kwItem;
    }
    country->setExpanded(true);
    if (lastAdded)
        m_tree->setCurrentItem(lastAdded);
}

void DialogKeywordTemplates::_removeTreeItem()
{
    QTreeWidgetItem *cur = m_tree->currentItem();
    if (!cur)
        return;
    delete cur; // removes it (and children if it's a country)
}

void DialogKeywordTemplates::accept()
{
    _commitCurrentTree();
    save(m_templates);
    QDialog::accept();
}
