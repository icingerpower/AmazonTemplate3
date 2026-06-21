#ifndef DIALOGCLASSIFICATIONTYPES_H
#define DIALOGCLASSIFICATIONTYPES_H

#include <QDialog>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

class QTableWidget;

struct UnknownClassification {
    QString classificationId;
    QString displayName;   // browseClassification.displayName
    QString exampleAsin;
    QString exampleTitle;  // product title from the WarningRow
};

// Modal dialog shown before upload when some ASINs have a classificationId but
// no productType. The user assigns an Amazon productType to each unknown
// classification (persisted via ClassificationTypeMap).
class DialogClassificationTypes : public QDialog
{
    Q_OBJECT
public:
    explicit DialogClassificationTypes(const QList<UnknownClassification> &unknowns,
                                       const QStringList &knownTypes,
                                       QWidget *parent = nullptr);

    // Returns classificationId → productType for entries the user filled in.
    QHash<QString, QString> result() const;

private:
    QTableWidget *m_table = nullptr;
};

#endif
