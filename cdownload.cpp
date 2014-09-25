#include "cdownload.h"
#include "mainwindow.h"
#include "cbkimages.h"

extern MainWindow *pcMainWnd;

///////////////////////////////////////
CDownload::CDownload(QObject *parent) :
///////////////////////////////////////
  QObject(parent)
{
}

////////////////////////////////////////////////////
void CDownload::begin(QString url, QString fileName)
////////////////////////////////////////////////////
{
  QUrl qurl(url + ".md5");

  //qDebug("url '%s'", qPrintable(url));

  QNetworkRequest request(qurl);
  QNetworkReply *reply = manager.get(request);

  m_fileName = fileName;

  connect(reply, SIGNAL(downloadProgress(qint64,qint64)), this, SLOT(slotProgress(qint64,qint64)));
  connect(&manager, SIGNAL(finished(QNetworkReply*)), this, SLOT(slotDownloadFinished(QNetworkReply*)));
  connect(this, SIGNAL(sigProgress(int,int)), pcMainWnd->m_pcDSSProg, SLOT(setProgressValue(int,int)));
  connect(this, SIGNAL(sigError(QString)), pcMainWnd, SLOT(slotDownloadError(QString)));

  pcMainWnd->setToolBoxPage(1);
}

///////////////////////////////////////////////////////
void CDownload::slotProgress(qint64 recv ,qint64 total)
///////////////////////////////////////////////////////
{
  //qDebug() << "recv : " << recv << " / " << total;
  if (recv == 0 || total == 0)
  {
    emit sigProgress((int)this, 0);
    return;
  }

  int p = 100 - (recv * 100 / (float)total);

  if (p == 0)
    p = 1;
  if (recv == total)
    p = 0;

  emit sigProgress((int)this, p);
}

//////////////////////////////////////////////////////////
void CDownload::slotDownloadFinished(QNetworkReply *reply)
//////////////////////////////////////////////////////////
{
  //qDebug("done %s", qPrintable(reply->errorString()));
  if (reply->error() == QNetworkReply::NoError)
  {
    SkFile f(m_fileName);

    if (f.open(SkFile::WriteOnly))
    {
      f.write(reply->readAll());
      f.close();
    }

    bkImg.load(m_fileName);
    pcMainWnd->repaintMap();
  }
  else
  { // error
    emit sigError(reply->errorString());
  }

  reply->deleteLater();
  deleteLater();
}

