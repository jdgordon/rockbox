/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 *   Copyright (C) 2007 by Dominik Wenger
 *   $Id$
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/


#ifndef VOICEFILE_H
#define VOICEFILE_H

#include <QtCore>
#include "progressloggerinterface.h"

#include "httpget.h"
#include "voicefont.h"
#include "talkgenerator.h"

class VoiceFileCreator :public QObject
{
    Q_OBJECT
public:
    VoiceFileCreator(QObject* parent);

    //start creation
    bool createVoiceFile();

    void setMountPoint(QString mountpoint) {m_mountpoint =mountpoint; }
    void setLang(QString name) { m_lang = name; }
    void setWavtrimThreshold(int th){m_wavtrimThreshold = th;}
    
public slots:
    void abort();
    
signals:
    void done(bool);
    void aborted();
    void logItem(QString, int); //! set logger item
    void logProgress(int, int); //! set progress bar.

private slots:
    void downloadDone(bool error);

private:

    void cleanup();

    HttpGet *getter;
    QString filename;  //the temporary file
    QString m_mountpoint;  //mountpoint of the device
    QString m_path;   //path where the wav and mp3 files are stored to
    int m_targetid;  //the target id
    QString m_lang;  // the language which will be spoken
    int m_wavtrimThreshold;

    bool m_abort;
    QList<TalkGenerator::TalkEntry> m_talkList;
};

#endif

