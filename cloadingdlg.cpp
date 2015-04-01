#include "cloadingdlg.h"
#include "ui_cloadingdlg.h"
#include "cdb.h"
#include "background.h"
#include "clunarfeatures.h"
#include "cwaitlogo.h"
#include "cconsole.h"
#include "cobjtracking.h"
#include "cdrawing.h"
#include "dsoplug.h"
#include "Usno2A.h"
#include "cucac4.h"
#include "cplanetrenderer.h"
#include "csgp4.h"
#include "csatellitedlg.h"

extern CPlanetRenderer  cPlanetRenderer;
extern QImage *g_pSunTexture;
extern QString g_horizonName;

CLoadingDlg::CLoadingDlg(QWidget *parent) :
  QDialog(parent),
  ui(new Ui::CLoadingDlg)
{
  ui->setupUi(this);

  const QRect screen = QApplication::desktop()->screenGeometry();
  move(screen.center() - this->rect().center());

  m_logo = new QPixmap(":/res/skytech.png");

  setWindowFlags(Qt::Popup);
  setFixedSize(size());

  ui->progressBar->setRange(0, 15);

  QTimer::singleShot(0, this, SLOT(slotLoad()));
}

CLoadingDlg::~CLoadingDlg()
{
  delete m_logo;
  delete ui;
}

void CLoadingDlg::changeEvent(QEvent *e)
{
  QDialog::changeEvent(e);
  switch (e->type()) {
  case QEvent::LanguageChange:
    ui->retranslateUi(this);
    break;
  default:
    break;
  }
}

void CLoadingDlg::paintEvent(QPaintEvent *)
{
  QPainter p(this);

  p.drawPixmap(0, 0, *m_logo);
}

void CLoadingDlg::sigProgress(int val)
{
  ui->progressBar->setValue(val);
  qApp->processEvents(QEventLoop::AllEvents);
}

void CLoadingDlg::slotLoad()
{
  QSettings set;

  setSetDefaultVal();

  g_setName = set.value("set_profile", "default").toString();
  qDebug("prof = %s", qPrintable(g_setName));
  setLoad(g_setName);

  cStarRenderer.open(g_skSet.map.starBitmapName);
  cPlanetRenderer.load();

  qDebug() << "L1";
  constLoad();
  sigProgress(1);

  qDebug() << "L2";
  cDSO.load();
  sigProgress(2);

  qDebug() << "L3";
  cGSCReg.loadRegions();
  sigProgress(3);

  qDebug() << "L4";
  cTYC.load();
  sigProgress(4);

  qDebug() << "L5";
  cMilkyWay.load();
  sigProgress(5);

  qDebug() << "L6";
  cSatXYZ.init();
  sigProgress(6);

  qDebug() << "L7";
  curAsteroidCatName = set.value("asteroid_file", "").toString();
  astLoad(curAsteroidCatName);
  sigProgress(7);

  qDebug() << "L8";
  curCometCatName = set.value("comet_file", "").toString();
  comLoad(curCometCatName);
  sigProgress(8);

  qDebug() << "L9";
  g_pSunTexture = new QImage(QStandardPaths::writableLocation(QStandardPaths::DataLocation) + "/data/sun/sun_tex.png");
  if (g_pSunTexture->isNull())
  {
    delete g_pSunTexture;
    g_pSunTexture = NULL;
  }
  sigProgress(9);

  qDebug() << "L10";
  g_pDb = new CDB(QSqlDatabase::addDatabase("QSQLITE", "sql_skytech"));
  g_pDb->setDatabaseName(QStandardPaths::writableLocation(QStandardPaths::DataLocation) + "/data/db/skytech.sql");
  qDebug() << QStandardPaths::writableLocation(QStandardPaths::DataLocation) + "/data/db/skytech.sql";
  if (g_pDb->open())
  {
    g_pDb->init();
  }

  sigProgress(10);

  qDebug() << "L11";
  g_horizonName = set.value("horizon_file", "none").toString();
  background.loadBackground(g_horizonName);

  sigProgress(11);
  cLunarFeatures.load("data/moon/moon.dat");

  sigProgress(12);
  loadTracking();

  sigProgress(13);
  drawingLoad();

  sigProgress(14);
  loadDSOPlugins();

  sigProgress(15);
  curSatelliteCatName = set.value("satellite_file", "").toString();
  sgp4.loadTLEData(curSatelliteCatName);

  usno.setUsnoDir(set.value("usno2_path", "").toString());
  cPPMXL.setDir(set.value("ppmxl_path", "").toString());
  cUcac4.setUCAC4Dir(set.value("ucac4_path", "").toString());

  done(0);
  qApp->processEvents(QEventLoop::AllEvents);
}

#if 0

///////////////////////////////////////////
CLoadingDlg::CLoadingDlg(QWidget *parent) :
  QDialog(parent),
  ui(new Ui::CLoadingDlg)
///////////////////////////////////////////
{
  ui->setupUi(this);

  const QRect screen = QApplication::desktop()->screenGeometry();
  move(screen.center() - this->rect().center());

  img = new QPixmap(":/res/skytech.png");

  ui->label_2->setPixmap(*img);

  tm.start();

  setWindowFlags(Qt::Popup);
  setFixedSize(size());

  ui->progressBar->setRange(0, 15);

  QSettings set;

  setSetDefaultVal();

  g_setName = set.value("set_profile", "default").toString();
  qDebug("prof = %s", qPrintable(g_setName));
  setLoad(g_setName);

  thread = new CLoading;
  thread->start();
  connect(thread, SIGNAL(sigEnd()), this, SLOT(slotEnd()));
  connect(thread, SIGNAL(sigProgress(int)), this, SLOT(slotProgress(int)));

  // load main thread dependent resources
  cStarRenderer.open(g_skSet.map.starBitmapName);
  cPlanetRenderer.load();

  m_waitLogo = new CWaitLogo(ui->label, QColor(255, 255, 255), 1);
}

///////////////////////////
CLoadingDlg::~CLoadingDlg()
///////////////////////////
{
  qDebug() << "Data loaded in" << tm.elapsed() / 1000. << "sec.";

  delete m_waitLogo;
  delete img;
  delete ui;
  delete thread;
}

////////////////////////////////////////
void CLoadingDlg::changeEvent(QEvent *e)
////////////////////////////////////////
{
  QDialog::changeEvent(e);
  switch (e->type()) {
  case QEvent::LanguageChange:
    ui->retranslateUi(this);
    break;
  default:
    break;
  }
}

///////////////////////////////////////////
void CLoadingDlg::paintEvent(QPaintEvent *)
///////////////////////////////////////////
{
  QPainter p(this);
}

///////////////////////////
void CLoadingDlg::slotEnd()
///////////////////////////
{
  thread->wait();
  done(0);
}

///////////////////////////////////////
void CLoadingDlg::slotProgress(int val)
///////////////////////////////////////
{
  QGraphicsColorizeEffect *eff = new QGraphicsColorizeEffect();

  eff->setStrength(CLAMP(1 - (val / (float)ui->progressBar->maximum()), 0.4f, 0.9f));
  eff->setColor(QColor(0, 0, 0));

  ui->label_2->setGraphicsEffect(eff);
  ui->progressBar->setValue(val);
}

////////////////////
void CLoading::run()
////////////////////
{
  qDebug() << "L1";
  constLoad();
  emit sigProgress(1);

  qDebug() << "L2";
  cDSO.load();
  emit sigProgress(2);

  qDebug() << "L3";
  cGSCReg.loadRegions();
  emit sigProgress(3);

  qDebug() << "L4";
  cTYC.load();
  emit sigProgress(4);

  qDebug() << "L5";
  cMilkyWay.load();
  emit sigProgress(5);

  qDebug() << "L6";
  cSatXYZ.init();
  emit sigProgress(6);

  qDebug() << "L7";
  QSettings set;
  curAsteroidCatName = set.value("asteroid_file", "").toString();
  astLoad(curAsteroidCatName);
  emit sigProgress(7);

  qDebug() << "L8";
  curCometCatName = set.value("comet_file", "").toString();
  comLoad(curCometCatName);
  emit sigProgress(8);

  qDebug() << "L9";
  g_pSunTexture = new QImage(QStandardPaths::writableLocation(QStandardPaths::DataLocation) + "/data/sun/sun_tex.png");
  if (g_pSunTexture->isNull())
  {
    delete g_pSunTexture;
    g_pSunTexture = NULL;
  }
  emit sigProgress(9);

  qDebug() << "L10";
  g_pDb = new CDB(QSqlDatabase::addDatabase("QSQLITE", "sql_skytech"));
  g_pDb->setDatabaseName(QStandardPaths::writableLocation(QStandardPaths::DataLocation) + "/data/db/skytech.sql");
  qDebug() << QStandardPaths::writableLocation(QStandardPaths::DataLocation) + "/data/db/skytech.sql";
  if (g_pDb->open())
  {
    g_pDb->init();
  }

  emit sigProgress(10);

  qDebug() << "L11";
  g_horizonName = set.value("horizon_file", "none").toString();
  background.loadBackground(g_horizonName);

  emit sigProgress(11);
  cLunarFeatures.load("data/moon/moon.dat");

  emit sigProgress(12);
  loadTracking();

  emit sigProgress(13);
  drawingLoad();

  emit sigProgress(14);
  loadDSOPlugins();

  emit sigProgress(15);
  curSatelliteCatName = set.value("satellite_file", "").toString();
  sgp4.loadTLEData(curSatelliteCatName);

  usno.setUsnoDir(set.value("usno2_path", "").toString());
  cPPMXL.setDir(set.value("ppmxl_path", "").toString());
  cUcac4.setUCAC4Dir(set.value("ucac4_path", "").toString());

  msleep(1000);

  emit sigEnd();
}





#endif
