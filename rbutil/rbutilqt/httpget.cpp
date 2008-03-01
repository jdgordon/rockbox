/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 *   Copyright (C) 2007 by Dominik Riebeling
 *   $Id$
 *
 * All files in this archive are subject to the GNU General Public License.
 * See the file COPYING in the source tree root for full license agreement.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/

#include <QtCore>
#include <QtNetwork>
#include <QtDebug>

#include "httpget.h"


HttpGet::HttpGet(QObject *parent)
    : QObject(parent)
{
    m_usecache = false;
    qDebug() << "--> HttpGet::HttpGet()";
    outputToBuffer = true;
    cached = false;
    getRequest = -1;
    // if a request is cancelled before a reponse is available return some
    // hint about this in the http response instead of nonsense.
    response = -1;

    connect(&http, SIGNAL(done(bool)), this, SLOT(httpDone(bool)));
    connect(&http, SIGNAL(dataReadProgress(int, int)), this, SLOT(httpProgress(int, int)));
    connect(&http, SIGNAL(requestFinished(int, bool)), this, SLOT(httpFinished(int, bool)));
    connect(&http, SIGNAL(responseHeaderReceived(const QHttpResponseHeader&)), this, SLOT(httpResponseHeader(const QHttpResponseHeader&)));
    connect(&http, SIGNAL(stateChanged(int)), this, SLOT(httpState(int)));
    connect(&http, SIGNAL(requestStarted(int)), this, SLOT(httpStarted(int)));

    connect(&http, SIGNAL(readyRead(const QHttpResponseHeader&)), this, SLOT(httpResponseHeader(const QHttpResponseHeader&)));

}


void HttpGet::setCache(QDir d)
{
    m_cachedir = d;
    bool result = true;

    QString p = m_cachedir.absolutePath() + "/rbutil-cache";
    if(QFileInfo(m_cachedir.absolutePath()).isDir())
    {
        if(!QFileInfo(p).isDir())
            result = m_cachedir.mkdir("rbutil-cache");
    }
    else result = false;
    qDebug() << "HttpGet::setCache(QDir)" << result;
    m_usecache = result;
}


void HttpGet::setCache(bool c)
{
    m_usecache = c;
}


QByteArray HttpGet::readAll()
{
    return dataBuffer;
}


QHttp::Error HttpGet::error()
{
    return http.error();
}


void HttpGet::httpProgress(int read, int total)
{
    emit dataReadProgress(read, total);
}


void HttpGet::setProxy(const QUrl &proxy)
{
    qDebug() << "HttpGet::setProxy" << proxy.toString();
    http.setProxy(proxy.host(), proxy.port(), proxy.userName(), proxy.password());
}


void HttpGet::setFile(QFile *file)
{
    outputFile = file;
    outputToBuffer = false;
    qDebug() << "HttpGet::setFile" << outputFile->fileName();
}


void HttpGet::abort()
{
    http.abort();
    if(!outputToBuffer)
        outputFile->close();
}


bool HttpGet::getFile(const QUrl &url)
{
    if (!url.isValid()) {
        qDebug() << "Error: Invalid URL" << endl;
        return false;
    }

    if (url.scheme() != "http") {
        qDebug() << "Error: URL must start with 'http:'" << endl;
        return false;
    }

    if (url.path().isEmpty()) {
        qDebug() << "Error: URL has no path" << endl;
        return false;
    }
    // if no output file was set write to buffer
    if(!outputToBuffer) {
        if (!outputFile->open(QIODevice::ReadWrite)) {
            qDebug() << "Error: Cannot open " << qPrintable(outputFile->fileName())
                << " for writing: " << qPrintable(outputFile->errorString())
                << endl;
            return false;
        }
    }
    // put hash generation here so it can get reused later
    QString hash = QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Md5).toHex();
    cachefile = m_cachedir.absolutePath() + "/rbutil-cache/" + hash;
    if(m_usecache) {
        // check if the file is present in cache
        qDebug() << "[HTTP] cache ENABLED for" << url.toEncoded();
        if(QFileInfo(cachefile).isReadable() && QFileInfo(cachefile).size() > 0) {
            qDebug() << "[HTTP] cached file found!" << cachefile;
            getRequest = -1;
            QFile c(cachefile);
            if(!outputToBuffer) {
                qDebug() << outputFile->fileName();
                c.open(QIODevice::ReadOnly);
                outputFile->open(QIODevice::ReadWrite);
                outputFile->write(c.readAll());
                outputFile->close();
                c.close();
            }
            else {
                c.open(QIODevice::ReadOnly);
                dataBuffer = c.readAll();
                c.close();
            }
            response = 200; // fake "200 OK" HTTP response
            cached = true;
            httpDone(false); // we're done now. This will emit the correct signal too.
            return true;
        }
        else qDebug() << "[HTTP] file not cached, downloading to" << cachefile;

    }
    else {
        qDebug() << "[HTTP] cache DISABLED";
    }
    http.setHost(url.host(), url.port(80));
    // construct query (if any)
    QList<QPair<QString, QString> > qitems = url.queryItems();
    if(url.hasQuery()) {
        query = "?";
        for(int i = 0; i < qitems.size(); i++)
            query += qitems.at(i).first + "=" + qitems.at(i).second + "&";
        qDebug() << query;
    }

    if(outputToBuffer) {
        qDebug() << "[HTTP] downloading to buffer:" << url.toString();
        getRequest = http.get(url.path() + query);
    }
    else {
        qDebug() << "[HTTP] downloading to file:" << url.toString() << qPrintable(outputFile->fileName());
        getRequest = http.get(url.path() + query, outputFile);
    }
    qDebug() << "[HTTP] request scheduled: GET" << getRequest;

    return true;
}


void HttpGet::httpDone(bool error)
{
    if (error) {
        qDebug() << "[HTTP] Error: " << qPrintable(http.errorString()) << httpResponse();
    }
    if(!outputToBuffer)
        outputFile->close();

    if(m_usecache && !cached) {
        qDebug() << "[HTTP] creating cache file" << cachefile;
        QFile c(cachefile);
        c.open(QIODevice::ReadWrite);
        if(!outputToBuffer) {
            outputFile->open(QIODevice::ReadOnly | QIODevice::Truncate);
            c.write(outputFile->readAll());
            outputFile->close();
        }
        else
            c.write(dataBuffer);

        c.close();
    }
    emit done(error);
}


void HttpGet::httpFinished(int id, bool error)
{
    qDebug() << "HttpGet::httpFinished(int, bool) =" << id << error;
    if(id == getRequest) dataBuffer = http.readAll();
    qDebug() << "pending:" << http.hasPendingRequests();
    //if(!http.hasPendingRequests()) httpDone(error);
    emit requestFinished(id, error);

}

void HttpGet::httpStarted(int id)
{
    qDebug() << "HttpGet::httpStarted(int) =" << id;
}


QString HttpGet::errorString()
{
    return http.errorString();
}


void HttpGet::httpResponseHeader(const QHttpResponseHeader &resp)
{
    // if there is a network error abort all scheduled requests for
    // this download
    response = resp.statusCode();
    if(response != 200) {
        qDebug() << "http response error:" << response << resp.reasonPhrase();
        http.abort();
    }
    // 301 -- moved permanently
    // 302 -- found
    // 303 -- see other
    // 307 -- moved temporarily
    // in all cases, header: location has the correct address so we can follow.
    if(response == 301 || response == 302 || response == 303 || response == 307) {
        // start new request with new url
        qDebug() << "http response" << response << "- following";
        getFile(resp.value("location") + query);
    }
}


int HttpGet::httpResponse()
{
    return response;
}


void HttpGet::httpState(int state)
{
    QString s[] = {"Unconnected", "HostLookup", "Connecting", "Sending",
        "Reading", "Connected", "Closing"};
    if(state <= 6)
        qDebug() << "HttpGet::httpState() = " << s[state];
    else qDebug() << "HttpGet::httpState() = " << state;
}

