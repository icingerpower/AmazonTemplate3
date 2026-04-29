// Compatibility stub for QXlsx installations that omit xlsxreadsax.h.
// The new version of xlsxdocument.h includes this header for its SAX streaming
// API, but some builds don't install it. This stub provides the required
// declarations so the project compiles without modifying the system headers.
// On older QXlsx versions that don't include xlsxreadsax.h at all, this file
// is never pulled in.
#ifndef XLSXREADSAX_H
#define XLSXREADSAX_H

#include <QXmlStreamReader>
#include <QString>
#include <QVariant>
#include <functional>

namespace QXlsx {

struct sax_options
{
    bool resolve_shared_strings = true;
    bool read_formulas_as_text = false;
    bool stop_on_empty_sheetdata = false;
};

struct sax_cell
{
    QString sheet_name;
    int row = 0;   // 1-based
    int col = 0;   // 1-based
    QVariant value;
};

using sax_cell_callback = std::function<bool(const sax_cell&)>;

class ZipReader;
QStringList load_shared_strings_all(ZipReader& zip);

bool read_sheet_xml_sax(const QByteArray& sheet_xml,
                        const sax_options& opt,
                        const QStringList* shared_strings,
                        const sax_cell_callback& on_cell);

} // namespace QXlsx

#endif // XLSXREADSAX_H
