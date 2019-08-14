/*
 * Copyright (C) 2019 UBports project.
 * Author(s): Marius Gripsgard <marius@ubports.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "xdgwatcher.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>

XdgWatcher::XdgWatcher(QObject* parent)
    : QObject(parent),
      m_watcher(new QFileSystemWatcher(this))
{
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &XdgWatcher::onDirectoryChanged);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &XdgWatcher::onFileChanged);

    const auto paths = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    for (const auto &path: paths) {
        const auto qdir = QDir(path);
        if (!qdir.exists()) {
            continue;
        }

        // Add the path itself to watch for newly added apps
        m_watcher->addPath(path);

        // Add watcher for eatch app to watch for changes
        const auto files = qdir.entryInfoList(QDir::Files);
        for (const auto &file: files) {
            if (file.suffix() == "desktop") {
                m_watcher->addPath(file.absoluteFilePath());
            }
        }
    }
}

// "Ubuntu style" appID is filename without versionNumber after last "_"
const QString XdgWatcher::stripAppIdVersion(const QString rawAppID) {
    auto appIdComponents = rawAppID.split("_");
    appIdComponents.removeLast();
    return appIdComponents.join("_");
}

// Standard appID see:
// https://standards.freedesktop.org/desktop-entry-spec/desktop-entry-spec-latest.html#desktop-file-id
const QString XdgWatcher::toStandardAppId(const QFileInfo fileInfo) const {
    const auto paths = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    for (const auto &path: paths) {
        if (fileInfo.absolutePath() == path) {
            break;
        }
        if (fileInfo.absolutePath().contains(path)) {
            auto fileStr = fileInfo.absoluteFilePath();
            fileStr.replace(path, "");
            fileStr.replace("/", "-");
            fileStr.replace(".desktop", "");
            return fileStr;
        }
    }
    return fileInfo.completeBaseName();
}

const QString XdgWatcher::toAppId(const QFileInfo fileInfo) const {
    QFile qFile(fileInfo.absoluteFilePath());
    qFile.open(QIODevice::ReadOnly);
    QTextStream fileStream(&qFile);
    QString line;
    while (fileStream.readLineInto(&line)) {
        if (line.startsWith("X-Ubuntu-Application-ID=")) {
            auto rawAppID = line.replace("X-Ubuntu-Application-ID=", "");
            qFile.close();
            return stripAppIdVersion(rawAppID);
        }
    }
    qFile.close();

    // If it's not an "Ubuntu" appID, we follow freedesktop standard
    return toStandardAppId(fileInfo);
}

// Watch for newly added apps
void XdgWatcher::onDirectoryChanged(const QString &path) {
    const auto files = QDir(path).entryInfoList(QDir::Files);
    const auto watchedFiles = m_watcher->files();
    for (const auto &file: files) {
        if (file.suffix() == "desktop" && !watchedFiles.contains(file.absoluteFilePath())) {
            m_watcher->addPath(file.absoluteFilePath());

            const auto appId = toAppId(file);
            Q_EMIT appAdded(appId);
        }
    }
}

void XdgWatcher::onFileChanged(const QString &path) {
    QFileInfo file(path);
    if (file.exists()) {
        // The file exists, this must be an modify event
        const auto appId = toAppId(file);
        Q_EMIT appInfoChanged(appId);
    } else {
        // File does not exist, assume this is an remove event.
        // onDirectoryChanged will handle rename event

        // As we have no way of checking the deleted file, we need to try remove both types
        // of appIDs.
        Q_EMIT appRemoved(toStandardAppId(file));
    }
}
