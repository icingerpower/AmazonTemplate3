#ifndef GSPRMANUFACTURERSTORE_H
#define GSPRMANUFACTURERSTORE_H

#include <QDir>
#include <QJsonArray>
#include <QList>
#include <QString>
#include <QStringList>

// One SKU-prefix → manufacturer contact mapping parsed from the supplier
// xlsx files. A warning SKU matches when it starts with `prefix`
// (case-insensitive); the longest matching prefix wins.
struct GsprManufacturerEntry {
    QString prefix;
    QString name;
    QString phone;
    QString address;
    QString email;
    QString url; // supplier contact/shop URL — Amazon accepts it as primary contact
};

// Reads every *.xlsx of the manufacturer folder (columns resolved by header
// names "Manufacturer" / "sku", order varies between files) and parses the
// free-form one-line manufacturer strings into name/phone/address/email.
//
// Results are cached in {folder}/gspr-manufacturer-cache.json keyed by file
// modification time: a file whose mtime is unchanged is never opened again.
// Rows that cannot be parsed are listed in unparseableRows() so the user can
// fix the data (parser validated at 634/636 rows across the real files).
class GsprManufacturerStore
{
public:
    void setFolder(const QDir &folder) { m_folder = folder; }

    // Parse new/changed files, reuse the cache for the rest. Returns false
    // when the folder is missing/empty. Safe to call repeatedly (mtime-based).
    bool reload(QString *error = nullptr);

    const QList<GsprManufacturerEntry> &entries() const { return m_entries; }
    QStringList unparseableRows() const { return m_unparseable; }
    QJsonArray entriesJson() const;

    // Exposed for tests: parse one manufacturer line / one sku cell.
    static bool parseManufacturer(const QString &raw, GsprManufacturerEntry *out);
    static QStringList skuPrefixes(const QString &cell, QStringList *badTokens = nullptr);

private:
    QDir m_folder;
    QList<GsprManufacturerEntry> m_entries;
    QStringList m_unparseable;
};

#endif // GSPRMANUFACTURERSTORE_H
