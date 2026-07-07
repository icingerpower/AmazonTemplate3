#ifndef DIALOGKEYWORDTEMPLATES_H
#define DIALOGKEYWORDTEMPLATES_H

#include <QDialog>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

class QListWidget;
class QTreeWidget;
class QListWidgetItem;

// A named set of title keywords per country, identified by a stable id so the
// display name can be renamed freely. Persisted in the working directory.
struct KeywordTemplate {
    QString id;
    QString name;
    QMap<QString, QStringList> byCountry; // country code (e.g. "FR") → keywords
};

// Editor for the keyword templates: a list of templates on the left, and for
// the selected one a tree of countries → keywords on the right.
class DialogKeywordTemplates : public QDialog
{
    Q_OBJECT
public:
    explicit DialogKeywordTemplates(QWidget *parent = nullptr);

    // Persistence (working-directory settings). Shared with the create dialog.
    static QList<KeywordTemplate> load();
    static void save(const QList<KeywordTemplate> &templates);
    // Keywords of template `id` for `country` (empty if none / not found).
    static QStringList keywordsFor(const QString &templateId, const QString &country);

    void accept() override;

private:
    void _reloadTemplateList();
    void _showTemplate(int index);
    void _commitCurrentTree(); // tree → m_templates[m_current].byCountry
    void _addTemplate();
    void _removeTemplate();
    void _addCountry();
    void _addKeyword();
    void _removeTreeItem();

    QList<KeywordTemplate> m_templates;
    int                    m_current = -1;

    QListWidget *m_list = nullptr;
    QTreeWidget *m_tree = nullptr;
};

#endif // DIALOGKEYWORDTEMPLATES_H
