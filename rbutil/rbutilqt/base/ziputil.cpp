/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 *   Copyright (C) 2011 Dominik Riebeling
 *
 * All files in this archive are subject to the GNU General Public License.
 * See the file COPYING in the source tree root for full license agreement.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/

#include <QtCore>
#include <QDebug>
#include "ziputil.h"
#include "progressloggerinterface.h"

#include "quazip.h"
#include "quazipfile.h"
#include "quazipfileinfo.h"


ZipUtil::ZipUtil(QObject* parent) : QObject(parent)
{
    m_zip = NULL;
}


ZipUtil::~ZipUtil()
{
    if(m_zip) {
        delete m_zip;
    }
}

//! @brief open zip file.
//! @param zipfile path to zip file
//! @param mode open mode (see QuaZip::Mode)
//! @return true on success, false otherwise
bool ZipUtil::open(QString& zipfile, QuaZip::Mode mode)
{
    m_zip = new QuaZip(zipfile);
    return m_zip->open(mode);
}


//! @brief close zip file.
//! @return true on success, false otherwise
bool ZipUtil::close(void)
{
    if(!m_zip) {
        return false;
    }

    int error = UNZ_OK;
    if(m_zip->isOpen()) {
        m_zip->close();
        error = m_zip->getZipError();
    }
    delete m_zip;
    m_zip = NULL;
    return (error == UNZ_OK) ? true : false;
}


//! @brief extract currently opened archive
//! @brief dest path to extract archive to
//! @return true on success, false otherwise
bool ZipUtil::extractArchive(QString& dest)
{
    bool result = true;
    if(!m_zip) {
        return false;
    }
    QuaZipFile *currentFile = new QuaZipFile(m_zip);
    int entries = m_zip->getEntriesCount();
    int current = 0;
    for(bool more = m_zip->goToFirstFile(); more; more = m_zip->goToNextFile())
    {
        ++current;
        // if the entry is a path ignore it. Path existence is ensured separately.
        if(m_zip->getCurrentFileName().split("/").last() == "")
            continue;

        QString outfilename = dest + "/" + m_zip->getCurrentFileName();
        QFile outputFile(outfilename);
        // make sure the output path exists
        if(!QDir().mkpath(QFileInfo(outfilename).absolutePath())) {
            result = false;
            emit logItem(tr("Creating output path failed"), LOGERROR);
            qDebug() << "[ZipUtil] creating output path failed for:"
                     << outfilename;
            break;
        }
        if(!outputFile.open(QFile::WriteOnly)) {
            result = false;
            emit logItem(tr("Creating output file failed"), LOGERROR);
            qDebug() << "[ZipUtil] creating output file failed:"
                     << outfilename;
            break;
        }
        currentFile->open(QIODevice::ReadOnly);
        outputFile.write(currentFile->readAll());
        if(currentFile->getZipError() != UNZ_OK) {
            result = false;
            emit logItem(tr("Error during Zip operation"), LOGERROR);
            qDebug() << "[ZipUtil] QuaZip error:" << currentFile->getZipError()
                     << "on file" << currentFile->getFileName();
            break;
        }
        currentFile->close();
        outputFile.close();

        emit logProgress(current, entries);
    }
    delete currentFile;
    emit logProgress(1, 1);

    return result;
}


//! @brief append a folder to current archive
//! @param source source folder
//! @param basedir base folder for archive. Will get stripped from zip paths.
//! @return true on success, false otherwise
bool ZipUtil::appendDirToArchive(QString& source, QString& basedir)
{
    bool result = true;
    if(!m_zip || !m_zip->isOpen()) {
        qDebug() << "[ZipUtil] Zip file not open!";
        return false;
    }
    // get a list of all files and folders. Needed for progress info and avoids
    // recursive calls.
    QDirIterator iterator(source, QDirIterator::Subdirectories);
    QStringList fileList;
    while(iterator.hasNext()) {
        iterator.next();
        // skip folders, we can't add them.
        if(!QFileInfo(iterator.filePath()).isDir()) {
            fileList.append(iterator.filePath());
        }
    }
    qDebug() << "[ZipUtil] Adding" << fileList.size() << "files to archive";

    int max = fileList.size();
    for(int i = 0; i < max; i++) {
        QString current = fileList.at(i);
        if(!appendFileToArchive(current, basedir)) {
            qDebug() << "[ZipUtil] Error appending file" << current
                     << "to archive" << m_zip->getZipName();
            result = false;
            break;
        }
        emit logProgress(i, max);
    }
    return result;
}


//! @brief append a single file to current archive
//!
bool ZipUtil::appendFileToArchive(QString& file, QString& basedir)
{
    bool result = true;
    if(!m_zip || !m_zip->isOpen()) {
        qDebug() << "[ZipUtil] Zip file not open!";
        return false;
    }
    // skip folders, we can't add them.
    QFileInfo fileinfo(file);
    if(fileinfo.isDir()) {
        return false;
    }
    QString infile = fileinfo.canonicalFilePath();
    QString newfile = infile;
    newfile.remove(QDir(basedir).canonicalPath() + "/");

    QuaZipFile fout(m_zip);
    QFile fin(file);

    if(!fin.open(QFile::ReadOnly)) {
        qDebug() << "[ZipUtil] Could not open file for reading:" << file;
        return false;
    }
    if(!fout.open(QIODevice::WriteOnly, QuaZipNewInfo(newfile, infile))) {
        fin.close();
        qDebug() << "[ZipUtil] Could not open file for writing:" << newfile;
        return false;
    }

    result = (fout.write(fin.readAll()) < 0) ? false : true;
    fin.close();
    fout.close();
    return result;
}


//! @brief calculate total size of extracted files
qint64 ZipUtil::totalUncompressedSize(unsigned int clustersize)
{
    qint64 uncompressed = 0;

    QList<QuaZipFileInfo> items = contentProperties();
    if(items.size() == 0) {
        return -1;
    }
    int max = items.size();
    if(clustersize > 0) {
        for(int i = 0; i < max; ++i) {
            qint64 item = items.at(i).uncompressedSize;
            uncompressed += (item + clustersize - (item % clustersize));
        }
    }
    else {
        for(int i = 0; i < max; ++i) {
            uncompressed += items.at(i).uncompressedSize;
        }
    }
    if(clustersize > 0) {
        qDebug() << "[ZipUtil] calculation rounded to cluster size for each file:"
                 << clustersize;
    }
    qDebug() << "[ZipUtil] size of archive files uncompressed:"
             << uncompressed;
    return uncompressed;
}


QStringList ZipUtil::files(void)
{
    QList<QuaZipFileInfo> items = contentProperties();
    QStringList fileList;
    if(items.size() == 0) {
        return fileList;
    }
    int max = items.size();
    for(int i = 0; i < max; ++i) {
        fileList.append(items.at(i).name);
    }
    return fileList;
}


QList<QuaZipFileInfo> ZipUtil::contentProperties()
{
    QList<QuaZipFileInfo> items;
    if(!m_zip || !m_zip->isOpen()) {
        qDebug() << "[ZipUtil] Zip file not open!";
        return items;
    }
    QuaZipFileInfo info;
    QuaZipFile currentFile(m_zip);
    for(bool more = m_zip->goToFirstFile(); more; more = m_zip->goToNextFile())
    {
        currentFile.getFileInfo(&info);
        if(currentFile.getZipError() != UNZ_OK) {
            qDebug() << "[ZipUtil] QuaZip error:" << currentFile.getZipError()
                     << "on file" << currentFile.getFileName();
            return QList<QuaZipFileInfo>();
        }
        items.append(info);
    }
    return items;
}

