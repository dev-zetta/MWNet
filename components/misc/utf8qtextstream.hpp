#ifndef OPENMW_COMPONENTS_MISC_UTF8QTEXTSTREAM_HPP
#define OPENMW_COMPONENTS_MISC_UTF8QTEXTSTREAM_HPP

#include <QtGlobal>

#include <QTextStream>

namespace Misc
{
    inline void ensureUtf8Encoding(QTextStream& stream)
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        stream.setEncoding(QStringConverter::Utf8);
#else
        stream.setCodec("UTF-8");
#endif
    }
}
#endif
