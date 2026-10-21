// TEMPORARY investigation instrumentation - remove once the C-arm/Vessel
// Screen jump during Patient -> Sheath -> Guide Catheter has been root-caused.
// Single shared log file across translation units (inline function-local
// statics are guaranteed to be one instance program-wide since C++11).
#pragma once
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QString>

inline void traceLog(const QString &msg) {
    static QFile f(QStringLiteral("C:/SurgeonsLab/EmboNav/trace_log.txt"));
    if (!f.isOpen())
        f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    QTextStream ts(&f);
    ts << QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz")) << "  " << msg << "\n";
    ts.flush();
    f.flush();
}
