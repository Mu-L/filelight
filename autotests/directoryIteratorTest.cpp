// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// SPDX-FileCopyrightText: 2022 Harald Sitter <sitter@kde.org>

#include <QDebug>
#include <QProcess>
#include <QTest>

#ifdef Q_OS_LINUX
#include <sys/param.h>
#endif

#include "directoryIterator.h"
#include "test-config.h"

using namespace Qt::StringLiterals;

class DirectoryIteratorTest : public QObject
{
    Q_OBJECT
    QString m_tree = QFINDTESTDATA("iterator-tree");
private Q_SLOTS:
    void testIterate()
    {
#if defined(ITERATOR_TREE_WITH_SYMLINK)
        const auto withSymlink = true;
#else
        const auto withSymlink = false;
#endif
#if defined(ITERATOR_TREE_WITH_LINK)
        const auto withLink = true;
#else
        const auto withLink = false;
#endif

        QMap<QByteArray, DirectoryEntry> entries;
        for (const auto &entry : DirectoryIterator(m_tree.toUtf8())) {
            qDebug() << entry.name;
            QVERIFY(!entries.contains(entry.name));
            entries.insert(entry.name, entry);
        }
        qDebug() << entries.keys();

        auto expectedEntries = 3;
        if (withSymlink) {
            ++expectedEntries;
        }
        if (withLink) {
            ++expectedEntries;
        }
        QCOMPARE(entries.size(), expectedEntries);

        QVERIFY(entries.contains(QByteArrayLiteral("foo")));

        QVERIFY(entries.contains(QByteArrayLiteral("Con 자백")));
        const auto dir = entries[QByteArrayLiteral("Con 자백")];
        QVERIFY(dir.isDir);
        QVERIFY(!dir.isFile);
        QVERIFY(!dir.isSkippable);
        // size doesn't matter, it's ignored

        QVERIFY(entries.contains(QByteArrayLiteral("bar")));
        const auto file = entries[QByteArrayLiteral("bar")];
        QVERIFY(!file.isDir);
        QVERIFY(file.isFile);
        QVERIFY(!file.isSkippable);
#ifdef Q_OS_WINDOWS
        QCOMPARE(file.size, 7682);
#elif defined(Q_OS_FREEBSD)
        // CI keeps changing, we don't assert anything for freebsd.
#elif defined(Q_OS_LINUX)
        QCOMPARE(file.size, 16 * DEV_BSIZE);
#else
        QCOMPARE(file.size, 16 * S_BLKSIZE);
#endif

        if (withSymlink) {
            QVERIFY(entries.contains(QByteArrayLiteral("symlink")));
            const auto symlink = entries[QByteArrayLiteral("symlink")];
            QVERIFY(!symlink.isDir);
            QVERIFY(!symlink.isFile);
            QVERIFY(symlink.isSkippable);
            // size of skippables doesn't matter
        }

        if (withLink) {
            QVERIFY(entries.contains(QByteArrayLiteral("link")));
            const auto symlink = entries[QByteArrayLiteral("link")];
            QVERIFY(!symlink.isDir);
            QVERIFY(symlink.isFile);
            QVERIFY(!symlink.isSkippable);
#ifdef Q_OS_WINDOWS
            QCOMPARE(symlink.size, 7682);
#elif defined(Q_OS_FREEBSD)
            // CI keeps changing, we don't assert anything for freebsd.
#else
            // We don't know the order, but one should be a duplicate
            QVERIFY(symlink.isDuplicate || file.isDuplicate);
            // Now make sure only one is a duplicate also
            QVERIFY(symlink.isDuplicate != file.isDuplicate);
#if defined(Q_OS_LINUX)
            QCOMPARE(symlink.size, 16 * DEV_BSIZE);
#else
            QCOMPARE(symlink.size, 16 * S_BLKSIZE);
#endif
#endif
        }
    }

    void testIterateInsideUnicode()
    {
        QByteArray tree = QFINDTESTDATA(u"iterator-tree/Con 자백"_s).toUtf8();
        QVERIFY(!tree.isEmpty());
        QMap<QByteArray, DirectoryEntry> entries;
        for (const auto &entry : DirectoryIterator(tree)) {
            qDebug() << entry.name;
            QVERIFY(!entries.contains(entry.name));
            entries.insert(entry.name, entry);
        }
        qDebug() << entries.keys();
    }

    // During development there were some bugs with iterating C:/, make sure this finishes eventually and has some entries.
    void testCDrive()
    {
        QMap<QByteArray, DirectoryEntry> entries;
        for (const auto &entry : DirectoryIterator(m_tree.toUtf8())) {
            QVERIFY(!entries.contains(entry.name));
            entries.insert(entry.name, entry);
        }
        QVERIFY(entries.size() > 3); // windows, programs, users
    }

    void testBadPath()
    {
        for (const auto &entry : DirectoryIterator(QByteArrayLiteral("/tmp/filelighttest1312312312313123123123"))) {
            Q_UNUSED(entry);
            QVERIFY(false);
        }
    }

    void testSparseFile()
    {
        QTemporaryDir tmpdir;
        QVERIFY(tmpdir.isValid());
        tmpdir.path();

        // cppreference for std::filesystem::resize_file says it will be sparse when possible but that is a lie!
#if defined(Q_OS_WINDOWS)
        {
            QProcess proc;
            proc.setWorkingDirectory(tmpdir.path());
            proc.start(QStringLiteral("fsutil"), {QStringLiteral("File"), QStringLiteral("CreateNew"), QStringLiteral("foo"), QStringLiteral("1000")});
            QVERIFY(proc.waitForFinished());
        }
        {
            QProcess proc;
            proc.setWorkingDirectory(tmpdir.path());
            proc.start(QStringLiteral("fsutil"), {QStringLiteral("Sparse"), QStringLiteral("SetFlag"), QStringLiteral("foo")});
            QVERIFY(proc.waitForFinished());
        }
        {
            QProcess proc;
            proc.setWorkingDirectory(tmpdir.path());
            proc.start(QStringLiteral("fsutil"),
                       {QStringLiteral("Sparse"), QStringLiteral("SetRange"), QStringLiteral("foo"), QStringLiteral("0"), QStringLiteral("1000")});
            QVERIFY(proc.waitForFinished());
        }
        for (const auto &entry : DirectoryIterator(tmpdir.path().toUtf8())) {
            QCOMPARE(entry.size, 0);
        }
#else
        {
            QProcess proc;
            proc.setWorkingDirectory(tmpdir.path());
            proc.start(QStringLiteral("truncate"), {QStringLiteral("-s"), QStringLiteral("1K"), QStringLiteral("foo")});
            QVERIFY(proc.waitForFinished());
        }
#endif

        bool found = false;
        for (const auto &entry : DirectoryIterator(tmpdir.path().toUtf8())) {
            QCOMPARE(entry.name, QByteArrayLiteral("foo"));
#if defined(Q_OS_FREEBSD)
            // CI keeps changing, we don't assert anything for freebsd.
#else
            QCOMPARE(entry.size, 0);
#endif
            QVERIFY(!found); // only one item
            found = true;
        }
        QVERIFY(found);
    }
};

QTEST_GUILESS_MAIN(DirectoryIteratorTest)

#include "directoryIteratorTest.moc"
