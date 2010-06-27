/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2010 Robert Bieber
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

#include "rbscreen.h"
#include "rbviewport.h"
#include "devicestate.h"

#include <QPainter>
#include <QFile>

RBScreen::RBScreen(const RBRenderInfo& info, bool remote,
                   QGraphicsItem *parent)
                       :QGraphicsItem(parent), backdrop(0), project(project)
{

    if(remote)
    {
        width = info.device()->data("remotewidth").toInt();
        height = info.device()->data("remoteheight").toInt();
    }
    else
    {
        width = info.device()->data("screenwidth").toInt();
        height = info.device()->data("screenheight").toInt();
    }

    QString bg = info.settings()->value("background color", "FFFFFF");
    bgColor = stringToColor(bg, Qt::white);

    QString fg = info.settings()->value("foreground color", "000000");
    fgColor = stringToColor(fg, Qt::black);

    settings = info.settings();

    /* Loading backdrop if available */
    themeBase = info.settings()->value("themebase", "");
    QString backdropFile = info.settings()->value("backdrop", "");
    backdropFile.replace("/.rockbox/backdrops/", "");

    if(QFile::exists(themeBase + "/backdrops/" + backdropFile))
    {
        backdrop = new QPixmap(themeBase + "/backdrops/" + backdropFile);

        /* If a backdrop has been found, use its width and height */
        if(!backdrop->isNull())
        {
            width = backdrop->width();
            height = backdrop->height();
        }
        else
        {
            delete backdrop;
            backdrop = 0;
        }
    }

    fonts.insert(0, new RBFont("Nothin'"));
}

RBScreen::~RBScreen()
{
    if(backdrop)
        delete backdrop;

    QMap<int, RBFont*>::iterator i;
    for(i = fonts.begin(); i != fonts.end(); i++)
        delete (*i);

    QMap<QString, QList<RBViewport*>*>::iterator it;
    for(it = namedViewports.begin(); it != namedViewports.end(); it++)
        delete (*it);
}

QPainterPath RBScreen::shape() const
{
    QPainterPath retval;
    retval.addRect(0, 0, width, height);
    return retval;
}

QRectF RBScreen::boundingRect() const
{
    return QRectF(0, 0, width, height);
}

void RBScreen::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                     QWidget *widget)
{
    if(backdrop)
    {
        painter->drawPixmap(0, 0, width, height, *backdrop);
    }
    else
    {
        painter->fillRect(0, 0, width, height, bgColor);
    }
}

void RBScreen::loadViewport(QString name, RBViewport *view)
{
    QList<RBViewport*>* list;
    if(namedViewports.value(name, 0) == 0)
    {
        list = new QList<RBViewport*>;
        list->append(view);
        namedViewports.insert(name, list);
    }
    else
    {
        list = namedViewports.value(name, 0);
        list->append(view);
    }
}

void RBScreen::showViewport(QString name)
{
    if(namedViewports.value(name, 0) == 0)
        return;

    QList<RBViewport*>* list = namedViewports.value(name, 0);
    for(int i = 0; i < list->count(); i++)
        list->at(i)->show();
}

void RBScreen::loadFont(int id, RBFont* font)
{
    if(id < 2 || id > 9)
        return;

    fonts.insert(id, font);
}

RBFont* RBScreen::getFont(int id)
{
    if(fonts.value(id, 0) != 0)
        return fonts.value(id);
    else
        return fonts.value(0, 0);
}


void RBScreen::setBackdrop(QString filename)
{

    if(backdrop)
        delete backdrop;

    filename = settings->value("imagepath", "") + "/" + filename;

    if(QFile::exists(filename))
        backdrop = new QPixmap(filename);
    else
        backdrop = 0;
}

void RBScreen::makeCustomUI(QString id)
{
    if(namedViewports.value(id, 0) != 0)
    {
        QMap<QString, QList<RBViewport*>*>::iterator i;
        for(i = namedViewports.begin(); i != namedViewports.end(); i++)
            for(int j = 0; j < (*i)->count(); j++)
                (*i)->at(j)->clearCustomUI();
        for(int i = 0; i < namedViewports.value(id)->count(); i++)
            namedViewports.value(id)->at(i)->makeCustomUI();
        for(int i = 0; i < namedViewports.value(id)->count(); i++)
            namedViewports.value(id)->at(i)->show();
    }
}

QColor RBScreen::stringToColor(QString str, QColor fallback)
{

    QColor retval;

    if(str.length() == 6)
    {
        for(int i = 0; i < 6; i++)
        {
            char c = str[i].toAscii();
            if(!((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') ||
                 isdigit(c)))
            {
                str = "";
                break;
            }
        }
        if(str != "")
            retval = QColor("#" + str);
        else
            retval = fallback;
    }
    else if(str.length() == 1)
    {
        if(isdigit(str[0].toAscii()) && str[0].toAscii() <= '3')
        {
            int shade = 255 * (str[0].toAscii() - '0') / 3;
            retval = QColor(shade, shade, shade);
        }
        else
        {
            retval = fallback;
        }
    }
    else
    {
        retval = fallback;
    }

    return retval;

}