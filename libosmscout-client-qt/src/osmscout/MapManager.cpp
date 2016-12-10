/*
  OSMScout - a Qt backend for libosmscout and libosmscout-map
  Copyright (C) 2016 Lukas Karas

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
*/

#include <QDirIterator>
#include <QDebug>

#include <osmscout/MapManager.h>
#include <osmscout/TypeConfig.h>
#include <osmscout/PersistentCookieJar.h>

#include <osmscout/util/Logger.h>
#include <osmscout/DBThread.h>

FileDownloadJob::FileDownloadJob(QUrl url, QFileInfo fileInfo):
  url(url), file(fileInfo.filePath()), downloading(false), downloaded(false), reply(NULL), bytesDownloaded(0)
{
  fileName=fileInfo.fileName();
}

void FileDownloadJob::start(QNetworkAccessManager *webCtrl)
{
  connect(webCtrl, SIGNAL(finished(QNetworkReply*)), this, SLOT(onFinished(QNetworkReply*)));
  qDebug() << "Downloading "<<url.toString()<<"to"<<file.fileName();

  // open file
  file.open(QIODevice::WriteOnly);
  
  // open connection
  QNetworkRequest request(url);    
  request.setHeader(QNetworkRequest::UserAgentHeader, QString(OSMSCOUT_USER_AGENT).arg(OSMSCOUT_VERSION_STRING));

  downloading=true;
  reply=webCtrl->get(request);
  connect(reply, SIGNAL(readyRead()), this, SLOT(onReadyRead()));
  connect(reply, SIGNAL(downloadProgress(qint64, qint64)), this, SLOT(onDownloadProgress(qint64, qint64)));
}

void FileDownloadJob::onDownloadProgress(qint64 bytesReceived, qint64 /*bytesTotal*/)
{
  bytesDownloaded=bytesReceived;
  emit downloadProgress();
}

void FileDownloadJob::onReadyRead()
{
  file.write(reply->readAll());
}

void FileDownloadJob::onFinished(QNetworkReply* reply)
{
  if (reply!=this->reply)
    return; // not for us
  
  // we are finished, we don't need to be connected anymore
  disconnect(QObject::sender(), SIGNAL(finished(QNetworkReply*)), this, SLOT(onFinished(QNetworkReply*)));

  downloading=false;
  file.close();
  if (reply->error() == QNetworkReply::NoError){
    downloaded=true;
    emit finished();
  }else{
    osmscout::log.Error() << "Downloading failed" << reply;
    emit failed();
  }  
  reply->deleteLater();
  this->reply=NULL;
}

MapDownloadJob::MapDownloadJob(QNetworkAccessManager *webCtrl, AvailableMapsModelMap map, QDir target):
  webCtrl(webCtrl), map(map), target(target), backoffInterval(1000), done(false), started(false)
{
  backoffTimer.setSingleShot(true);
  connect(&backoffTimer, SIGNAL(timeout()), this, SLOT(downloadNextFile()));
}

MapDownloadJob::~MapDownloadJob()
{
  for (auto job:jobs){
    delete job;
  }
  jobs.clear();
}

void MapDownloadJob::start()
{
  QStringList fileNames;
  fileNames << "bounding.dat"
            << "nodes.dat"
            << "areas.dat"
            << "ways.dat" 
            << "areanode.idx"
            << "areaarea.idx"
            << "areaway.idx" 
            << "areasopt.dat"
            << "waysopt.dat" 
            << "location.idx"
            << "water.idx"
            << "intersections.dat"                                                                                                                                                                                                                                                                                         
            << "intersections.idx"                                                                                                                                                                                                                                                                                         
            << "router.dat"
            << "router2.dat"
            << "router.idx"
            << "textloc.dat"
            << "textother.dat"
            << "textpoi.dat"
            << "textregion.dat";

  // types.dat should be last, when download is interrupted,
  // directory is not recognized as valid map
  fileNames << "types.dat";

  for (auto fileName:fileNames){
    auto job=new FileDownloadJob(map.getProvider().getUri()+"/"+map.getServerDirectory()+"/"+fileName, target.filePath(fileName));
    connect(job, SIGNAL(finished()), this, SLOT(onJobFinished()));
    connect(job, SIGNAL(failed()), this, SLOT(onJobFailed()));
    connect(job, SIGNAL(downloadProgress()), this, SIGNAL(downloadProgress()));
    jobs << job; 
  }
  started=true;
  downloadNextFile();
}

void MapDownloadJob::onJobFailed(){
  osmscout::log.Debug() << "Continue downloading after"<<backoffInterval<<"ms";
  backoffTimer.setInterval(backoffInterval); 
  backoffInterval=std::min(backoffInterval * 2, 10*60*1000);
  backoffTimer.start();
}
void MapDownloadJob::onJobFinished()
{
  backoffInterval=1000;
  downloadNextFile();
}

void MapDownloadJob::downloadNextFile()
{
  //qDebug() << "current thread:"<<QThread::currentThread();
  for (auto job:jobs){
    if (!job->isDownloaded()){
      job->start(webCtrl);
      return;
    }
  }
  done=true;
  emit finished();
}

double MapDownloadJob::getProgress()
{
  double expected=expectedSize();
  size_t downloaded=0;
  for (auto job:jobs){
    downloaded+=job->getBytesDownloaded();
  }
  if (expected==0.0)
    return 0;
  return (double)downloaded/expected;
}

QString MapDownloadJob::getDownloadingFile()
{
  for (auto job:jobs){
    if (job->isDownloading()){
      return job->getFileName();
    }
  }
  return "";
}

MapManager::MapManager(QStringList databaseLookupDirs):
  databaseLookupDirs(databaseLookupDirs)
{
  webCtrl.setCookieJar(new PersistentCookieJar());
  // we don't use disk cache here

}

void MapManager::lookupDatabases()
{
  databaseDirectories.clear();

  for (QString lookupDir:databaseLookupDirs){
    QDirIterator dirIt(lookupDir, QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
    while (dirIt.hasNext()) {
      dirIt.next();
      QFileInfo fInfo(dirIt.filePath());
      if (fInfo.isFile() && fInfo.fileName() == osmscout::TypeConfig::FILE_TYPES_DAT){
        qDebug() << "found database: " << fInfo.dir().absolutePath();
        databaseDirectories << fInfo.dir();
      }
    }
  }
  emit databaseListChanged(databaseDirectories);
}

MapManager::~MapManager(){
  for (auto job:downloadJobs){
    delete job;
  }
  downloadJobs.clear();
}

void MapManager::downloadMap(AvailableMapsModelMap map, QDir dir)
{
  if (dir.exists()){
    qWarning() << "Directory already exists"<<dir.path()<<"!";
    return;
  }
  if (!dir.mkdir(dir.path())){
    qWarning() << "Can't create directory"<<dir.path()<<"!";
    return;
  }
  QStorageInfo storage=QStorageInfo(dir);
  if (storage.bytesAvailable()<(double)map.getSize()){
    qWarning() << "Free space"<<storage.bytesAvailable()<<" bytes is less than map size ("<<map.getSize()<<")!";
  }
  
  auto job=new MapDownloadJob(&webCtrl, map, dir);
  connect(job, SIGNAL(finished()), this, SLOT(onJobFinished()));
  downloadJobs<<job;
  emit downloadJobsChanged();
  downloadNext();
}

void MapManager::downloadNext()
{
  for (auto job:downloadJobs){
    if (job->isDownloading()){
      return;
    }
  }  
  for (auto job:downloadJobs){
    job->start();
    break;
  }  
}

void MapManager::onJobFinished()
{
  QList<MapDownloadJob*> finished;
  for (auto job:downloadJobs){
    if (job->isDone()){
      finished<<job;
    }
  }
  if (!finished.isEmpty()){
    lookupDatabases();
  }
  for (auto job:finished){
    downloadJobs.removeOne(job);
    emit downloadJobsChanged();
    delete job;
  }
  downloadNext();
}
