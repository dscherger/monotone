#ifndef VOCAB_H
#define VOCAB_H

//
// global macros and defines
//

// if you use any of those two, you also have to include Guitone.h
class Guitone;
#define APP reinterpret_cast<Guitone*>(qApp)
#define MTN(arg) APP->getMonotoneInstance(arg)

#include "DebugLog.h"
#ifdef QT_NO_DEBUG
#define D(msg) void(msg)
#else
#define D(msg) DebugLog::debug(QString("%1:%2:%3: %4") \
        .arg(__FILE__).arg(__FUNCTION__).arg(__LINE__).arg(QString(msg)))
#endif

#define L(msg) DebugLog::info(QString("%1:%2:%3: %4") \
        .arg(__FILE__).arg(__FUNCTION__).arg(__LINE__).arg(QString(msg)))
#define W(msg) DebugLog::warn(QString("%1:%2:%3: %4") \
        .arg(__FILE__).arg(__FUNCTION__).arg(__LINE__).arg(QString(msg)))
#define C(msg) DebugLog::critical(QString("%1:%2:%3: %4") \
        .arg(__FILE__).arg(__FUNCTION__).arg(__LINE__).arg(QString(msg)))
#define F(msg) { \
          DebugLog::fatal(QString("%1:%2:%3: %4") \
          .arg(__FILE__).arg(__FUNCTION__).arg(__LINE__).arg(QString(msg))); \
          abort(); \
        }

#define I(expr) if (!(expr)) F(QString("invariant \"%1\" violated").arg(#expr))

//
// type definitions
//

#include <QMap>
#include <QPair>
#include <QList>
#include <QStringList>

// used for manifest entries, if the bool var is true, the entry is a directory
struct FileEntry {
    FileEntry() {}
    FileEntry(QString p, bool d) : path(p), is_dir(d) {}
    FileEntry(QString p, bool d, QString f) : path(p), is_dir(d), fileid(f) {}
    QString path;
    bool is_dir;
    QString fileid;
    QMap<QString, QString> attrs;

    inline bool operator<(const FileEntry & other) const
    {
        return path < other.path;
    }
};

typedef QList<FileEntry> FileEntryList;

typedef QStringList RevisionList;

// used for revision certs
typedef QString CertKey;
typedef QString CertValue;
typedef QPair<CertKey, CertValue> RevisionCert;
typedef QList<RevisionCert> RevisionCerts;

// used to store the output of automate certs
typedef struct {
    enum Trust { Trusted, Untrusted } trust;
    enum Signature { Ok, Bad, Unknown } signature;
    QString key;
    QString name;
    QString value;
} Cert;
typedef QList<Cert> CertList;

typedef QList<QByteArray> ByteArrayList;

// used for BasicIOParser and BasicIOWriter
struct StanzaEntry {
    StanzaEntry() {}
    StanzaEntry(QString s, QString h) : sym(s), hash(h) {}
    StanzaEntry(QString s, QStringList v) : sym(s), vals(v) {}

    QString sym;
    QString hash;
    QStringList vals;
};

typedef QList <StanzaEntry> Stanza;
typedef QList <Stanza> StanzaList;

typedef QString GuitoneException;

// TODO: maybe we can load workspace normalization into this
typedef QString WorkspacePath;
typedef QString DatabaseFile;

#endif
