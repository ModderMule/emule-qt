#pragma once

/// @file NzbFile.h
/// @brief Parsing an .nzb into an NzbInfo, with QXmlStreamReader.
///
/// No libxml2. NZBGet uses a libxml2 SAX parser only because it has no XML
/// runtime of its own; Qt does, `QXmlStreamReader` lives in Qt6::Core, and the
/// NZB schema is small enough that a pull parser reads more like the document
/// than a callback soup does. ngPost's NzbCheck already does exactly this.
///
/// This is the first QXmlStreamReader in the codebase. The same reader will do
/// for newznab RSS in phase 5.
///
/// The schema, in full:
///
///   <nzb xmlns="http://www.newzbin.com/DTD/2003/nzb">
///     <head><meta type="password">secret</meta></head>
///     <file poster="..." date="1234567890" subject="...">
///       <groups><group>alt.binaries.x</group></groups>
///       <segments><segment bytes="716800" number="1">msgid@host</segment></segments>
///     </file>
///   </nzb>
///
/// Namespaces are ignored on purpose: real NZBs in the wild carry the newzbin
/// namespace, a wrong namespace, or none at all, and refusing any of those
/// helps nobody.

#include "nzb/NzbInfo.h"

#include <QString>

class QByteArray;
class QIODevice;

namespace eMule::usenet {

class NzbFile {
public:
    /// Parse @p data. On failure returns false and sets @p error to something a
    /// user can act on. A file that parses but contains no <file> elements is
    /// a *failure*, not an empty success: it is almost always an HTML error page
    /// an indexer served instead of the NZB.
    static bool parse(const QByteArray& data, NzbInfo& out, QString& error);

    /// Read and parse @p path. `out.name` is seeded from the file's base name,
    /// and the password is taken from the filename when the NZB carries no
    /// <meta type="password"> — "Release{{secret}}.nzb" and "Release.nzb" both
    /// being conventions in the wild.
    static bool parseFile(const QString& path, NzbInfo& out, QString& error);

    /// Extract an embedded password from an .nzb *filename*. Recognises the two
    /// conventions NZBGet does: `name{{password}}.nzb` and `name password.nzb`
    /// is deliberately NOT one of them — too many false positives.
    /// Returns an empty string when there is none, and strips the marker from
    /// @p baseName in place.
    static QString takePasswordFromName(QString& baseName);

private:
    NzbFile() = delete;
};

} // namespace eMule::usenet
