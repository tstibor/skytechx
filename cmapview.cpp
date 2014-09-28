#include <QtGui>
#include <QPrinter>
#include <QPrintDialog>
#include <omp.h>

#include "cmapview.h"
#include "skcore.h"
#include "tycho.h"
#include "cgscreg.h"
#include "cstarrenderer.h"
#include "transform.h"
#include "Gsc.h"
#include "cskpainter.h"
#include "skymap.h"
#include "jd.h"
#include "setting.h"
#include "mainwindow.h"
#include "precess.h"
#include "cshape.h"
#include "mapobj.h"
#include "cbkimages.h"
#include "castro.h"
#include "cobjfillinfo.h"
#include "precess.h"
#include "cteleplug.h"
#include "cdrawing.h"
#include "cgeohash.h"
#include "cgetprofile.h"

extern bool g_developMode;
extern bool g_lockFOV;

QCursor cur_rotate;

MainWindow    *pcMainWnd;
QElapsedTimer  timer;
CBkImages      bkImg;
CMapView      *pcMapView;
QString        helpText;

extern bool    g_bHoldObject;
extern double  g_dssRa;
extern double  g_dssDec;
extern double  g_dssSize;
extern bool    g_dssUse;
extern bool    g_ddsSimpleRect;
extern radec_t g_dssCorner[4];
extern bool    g_antialiasing;
extern bool    g_showZoomBar;

///////////////////////////////////


typedef struct
{
  QString         name;
  int             typeId;
  QList <radec_t> shapes;
} dev_shape_t;

bool bConstEdit = false;
bool bDevelopMode = false;

int                 dev_move_index = -1;
int                 dev_shape_index = -1;
int                 dev_const_type = 1;
int                 dev_const_sel = -1;
static QList <dev_shape_t> dev_shapes;


//////////////////////////////
void setHelpText(QString text)
//////////////////////////////
{
  helpText = text;
}

/////////////////////////////////////
CMapView::CMapView(QWidget *parent) :
  QWidget(parent)
/////////////////////////////////////
{
  QSettings settings;
  pBmp = new QImage;
  pcMapView = this;

  m_demo = new CDemonstration();
  m_demo->setupPoints();
  connect(m_demo, SIGNAL(sigAnimChanged(curvePoint_t&)), this, SLOT(slotAnimChanged(curvePoint_t&)));
  //m_demo->start();

  m_zoom = new CZoomBar(this);

  //setToolTip("");

  connect(m_zoom, SIGNAL(sigZoom(float)), this, SLOT(slotZoom(float)));

  cur_rotate = QCursor(QPixmap(":/res/cur_rotate.png"));

  setMouseTracking(true);
  //grabKeyboard();

  setAttribute(Qt::WA_OpaquePaintEvent, true);
  setAttribute(Qt::WA_NoSystemBackground, true);

  m_mapView.coordType = SMCT_RA_DEC;
  m_mapView.jd = jdGetCurrentJD();
  m_mapView.deltaT = CM_UNDEF;
  m_mapView.deltaTAlg = DELTA_T_ESPENAK_MEEUS_06;
  m_mapView.x = 0;
  m_mapView.y = 0;
  m_mapView.roll = 0;
  m_mapView.fov = D2R(90);

  m_mapView.starMag = 0;
  m_mapView.starMagAdd = 0;

  m_mapView.dsoMag = 0;
  m_mapView.dsoMagAdd = 0;

  m_mapView.flipX = false;
  m_mapView.flipY = false;

  m_mapView.deltaT = settings.value("delta_t/delta_t", CM_UNDEF).toDouble();
  m_mapView.deltaTAlg = settings.value("delta_t/delta_t_alg", DELTA_T_ESPENAK_MEEUS_06).toInt();

  m_mapView.geo.lon = settings.value("geo/longitude", D2R(15)).toDouble();
  m_mapView.geo.lat = settings.value("geo/latitude", D2R(50)).toDouble();
  m_mapView.geo.alt = settings.value("geo/altitude", 100).toDouble();
  m_mapView.geo.tzo = settings.value("geo/tzo", 1 / 24.0).toDouble();
  m_mapView.geo.sdlt = settings.value("geo/sdlt", 0).toDouble();
  m_mapView.geo.temp = settings.value("geo/temp", 15).toDouble();
  m_mapView.geo.press = settings.value("geo/press", 1013).toDouble();
  m_mapView.geo.name = settings.value("geo/name", "Unnamed").toString();

  m_mapView.geo.tz = m_mapView.geo.tzo + m_mapView.geo.sdlt;
  m_mapView.geo.hash = CGeoHash::calculate(&m_mapView.geo);

  g_autoSave.drawing = settings.value("saving/drawing", true).toBool();
  g_autoSave.events = settings.value("saving/events", true).toBool();
  g_autoSave.tracking = settings.value("saving/tracking", true).toBool();

  m_lastStarMag = 0;
  m_lastDsoMag = 0;

  m_magLock = false;

  m_measurePoint.Ra = 0;
  m_measurePoint.Dec = 0;

  m_bClick = false;
  m_bMouseMoveMap = false;
  m_bZoomByMouse = false;
  m_zoomCenter = false;
  m_drawing = false;

  m_bCustomTele = false;

  m_bInit = false;
}


/////////////////////
CMapView::~CMapView()
/////////////////////
{
  m_bInit = false;
}


///////////////////////////////////////////
void CMapView::resizeEvent(QResizeEvent *e)
///////////////////////////////////////////
{
  delete pBmp;
  pBmp = new QImage(e->size().width(), e->size().height(), QImage::Format_ARGB32_Premultiplied);

  m_bInit = true;

  m_zoom->setObjAlign(2);

  repaintMap();
}

/////////////////////////////////////////
void CMapView::wheelEvent(QWheelEvent *e)
/////////////////////////////////////////
{
  double mul = 1;

  if (e->modifiers() & Qt::ShiftModifier)
    mul = 0.1;

  // TODO: neni to linearni (dopredu a dozadu neda tu samou hodnotu)
  //else
  //if (e->modifiers() & Qt::ControlModifier)
  //  mul = 2;

  if (e->delta() > 0)
    addFov(1, 0.5 * mul);
  else
    addFov(-1, 0.5 * mul);

  repaintMap();
  setFocus();
}


//////////////////////////////////////////////
void CMapView::mousePressEvent(QMouseEvent *e)
//////////////////////////////////////////////
{
  setFocus();

  m_lastMousePos = e->pos();

  if (bConstEdit)
  {
    if ((e->buttons() & Qt::LeftButton) == Qt::LeftButton)
    {
      QList <constelLine_t> *list = constGetLinesList();
      int r = 8;
      QRect rc(e->pos().x() - r, e->pos().y() - r, r * 2, r * 2);
      for (int i = 0; i < list->count(); i++)
      {
        radec_t rd;
        SKPOINT pt;

        rd = list->at(i).pt;
        trfRaDecToPointNoCorrect(&rd, &pt);
        if (trfProjectPoint(&pt))
        {
          if (rc.contains(pt.sx, pt.sy))
          {
            dev_move_index = i;
            dev_const_sel = i;
            update();
            return;
          }
        }
      }
    }
  }

  if (bDevelopMode)
  {
    if (((e->buttons() & Qt::LeftButton) == Qt::LeftButton) && dev_shape_index != -1)
    {
      int r = 5;
      QRect rc(e->pos().x() - r, e->pos().y() - r, r * 2, r * 2);
      for (int i = 0; i < dev_shapes[dev_shape_index].shapes.count(); i++)
      {
        radec_t rd;
        SKPOINT pt;

        rd = dev_shapes[dev_shape_index].shapes[i];

        trfRaDecToPointNoCorrect(&rd, &pt);

        if (trfProjectPoint(&pt))
        {
          if (rc.contains(pt.sx, pt.sy))
          {
            dev_move_index = i;
            return;
          }
        }
      }
    }
  }

  if ((e->buttons() & Qt::LeftButton) == Qt::LeftButton)
  {
    m_dto = g_cDrawing.editObject(e->pos(), QPoint(0, 0));
    if (m_dto != DTO_NONE)
    {
      m_drawing = true;
      if (m_dto == DTO_ROTATE)
        setCursor(cur_rotate);
      else
        setCursor(Qt::SizeAllCursor); // udelat podle operace
      return;
    }
  }

  if ((e->buttons() & Qt::RightButton) == Qt::RightButton)
  {
    mapObjContextMenu(this);
    repaintMap();
  }

  if ((e->buttons() & Qt::LeftButton) == Qt::LeftButton)
  {
    m_bClick = true;
    m_bZoomByMouse = true;
    m_zoomPoint = e->pos();

    if ((e->modifiers() & Qt::ShiftModifier))
      m_zoomCenter = true;
    else
      m_zoomCenter = false;
  }

  if ((e->modifiers() & Qt::ControlModifier) || e->button() == Qt::MidButton)
  {
    m_bMouseMoveMap = true;
    m_bZoomByMouse = false;
  }
}


/////////////////////////////////////////////
void CMapView::mouseMoveEvent(QMouseEvent *e)
/////////////////////////////////////////////
{
  /*
  double mul = 1;

  if (e->modifiers() & Qt::ShiftModifier)
    mul = 0.1;
  */

  //QPoint delta = m_lastMousePos - e->pos();

  if (bConstEdit && (e->buttons() & Qt::LeftButton) == Qt::LeftButton)
  {
    if (dev_move_index != -1)
    {
      radec_t rd;

      trfConvScrPtToXY(e->pos().x(), e->pos().y(), rd.Ra, rd.Dec);
      tConstLines[dev_move_index].pt = rd;

      m_lastMousePos = e->pos();
      repaintMap(false);
      return;
    }
  }


  //////////////////////////////////////////////////
  if (bDevelopMode && (e->buttons() & Qt::LeftButton) == Qt::LeftButton)
  {
    if ((e->modifiers() & Qt::CTRL) && dev_move_index != -1)
    {
      radec_t curRd;
      radec_t prvRd;

      trfConvScrPtToXY(e->pos().x(), e->pos().y(), curRd.Ra, curRd.Dec);
      trfConvScrPtToXY(m_lastMousePos.x(), m_lastMousePos.y(), prvRd.Ra, prvRd.Dec);

      double dRa  = prvRd.Ra - curRd.Ra;
      double dDec = prvRd.Dec - curRd.Dec;

      //qDebug("RD = %f", dRa);

      for (int i = 0; i < dev_shapes[dev_shape_index].shapes.count(); i++)
      {
        dev_shapes[dev_shape_index].shapes[i].Ra  -= dRa;
        dev_shapes[dev_shape_index].shapes[i].Dec -= dDec;
      }

      m_lastMousePos = e->pos();

      repaintMap(false);
      return;
    }
    else
    if (dev_move_index != -1)
    {
      radec_t rd;

      trfConvScrPtToXY(e->pos().x(), e->pos().y(), rd.Ra, rd.Dec);
      dev_shapes[dev_shape_index].shapes[dev_move_index] = rd;

      repaintMap(false);
      return;
    }
  }

  //////////////////////////////////////////////////


  m_bClick = false;

  if (m_bMouseMoveMap)
  {
    radec_t rd1;
    radec_t rd2;

    trfConvScrPtToXY(e->pos().x(), e->pos().y(), rd1.Ra, rd1.Dec);
    trfConvScrPtToXY(m_lastMousePos.x(), m_lastMousePos.y(), rd2.Ra, rd2.Dec);

    if (m_mapView.coordType == SMCT_ALT_AZM)
    {
      cAstro.convRD2AARef(rd1.Ra, rd1.Dec, &rd1.Ra, &rd1.Dec);
      cAstro.convRD2AARef(rd2.Ra, rd2.Dec, &rd2.Ra, &rd2.Dec);
      qSwap(rd1.Ra, rd2.Ra);
    }
    else
    if (m_mapView.coordType == SMCT_ECL)
    {
      cAstro.convRD2Ecl(rd1.Ra, rd1.Dec, &rd1.Ra, &rd1.Dec);
      cAstro.convRD2Ecl(rd2.Ra, rd2.Dec, &rd2.Ra, &rd2.Dec);
    }

    double rad = rd1.Ra - rd2.Ra;
    double ded = rd1.Dec - rd2.Dec;

    m_mapView.x -= rad;
    m_mapView.y -= ded;

    rangeDbl(&m_mapView.x, R360);
    m_mapView.y = CLAMP(m_mapView.y, -R90, R90);

    setCursor(QCursor(Qt::SizeAllCursor));
    repaintMap(true);
  }

  /*
  if (m_bMouseMoveMap)
  {
    double mulX = mul;
    double mulY = mul;

    if (m_mapView.flipX)
      mulX *= -mulX;

    if (m_mapView.flipY)
      mulY *= -mulY;

    addX(-0.002 * m_mapView.fov * delta.x() * mulX);
    addY(-0.002 * m_mapView.fov * delta.y() * mulY);
    repaintMap(true);
  }
  */

  if (((e->buttons() & Qt::LeftButton) == Qt::LeftButton) && m_drawing)
  {
    if (g_cDrawing.editObject(e->pos(), QPoint(m_lastMousePos - e->pos()), m_dto))
    {
      m_bZoomByMouse = false;
      m_drawing = true;
      if (m_dto == DTO_ROTATE)
        setCursor(cur_rotate);
      else
        setCursor(Qt::SizeAllCursor); // udelat podle operace
    }
  }
  else
  if (e->buttons() == 0)
  {
    m_dto = g_cDrawing.editObject(e->pos(), QPoint(0, 0));
    if (m_dto != DTO_NONE)
    {
      if (m_dto == DTO_ROTATE)
        setCursor(cur_rotate);
      else
        setCursor(Qt::SizeAllCursor); // udelat podle operace
      return;
    }
    else
    {
      setCursor(Qt::CrossCursor);
    }

  }

  m_lastMousePos = e->pos();
  repaintMap(false);
}


///////////////////////////////////////////////
void CMapView::mouseReleaseEvent(QMouseEvent *e)
///////////////////////////////////////////////
{
  //qDebug("release");
  setCursor(QCursor(Qt::CrossCursor));

  m_bMouseMoveMap = false;
  m_drawing = false;

  if (bConstEdit && dev_move_index != -1)
  {
    int r = 10;
    QRect rc(e->pos().x() - r, e->pos().y() - r, r * 2, r * 2);
    radec_t rd;

    if (mapObjSnap(e->pos().x(), e->pos().y(), &rd))
    {
      tConstLines[dev_move_index].pt = rd;
      repaintMap(false);
    }

    dev_move_index = -1;
  }

  if (bDevelopMode && dev_move_index != -1)
  {
    if ((e->modifiers() & Qt::AltModifier))
    { // snap to
      int r = 10;
      QRect rc(e->pos().x() - r, e->pos().y() - r, r * 2, r * 2);

      for (int j = 0; j < dev_shapes.count(); j++)
      {
        for (int i = 0; i < dev_shapes[j].shapes.count(); i++)
        {
          radec_t rd;
          SKPOINT pt;

          if (dev_shape_index == j && dev_move_index == i)
            continue;

          rd = dev_shapes[j].shapes[i];

          trfRaDecToPointNoCorrect(&rd, &pt);

          if (trfProjectPoint(&pt))
          {
            if (rc.contains(pt.sx, pt.sy))
            {
              if (dev_shape_index == j)
              {
                msgBoxError(this, "Vertex can't be snaped to same shape!!!");
                dev_move_index = -1;
                repaintMap(false);
                return;
              }

              dev_shapes[dev_shape_index].shapes[dev_move_index] = rd;
              dev_move_index = -1;
              repaintMap(false);
              return;
            }
          }
        }
      }
    }

    dev_move_index = -1;
  }

  if (m_bClick && !(e->modifiers() & Qt::ControlModifier))
  { // search object
    mapObj_t obj;

    if (mapObjSearch(e->pos().x(), e->pos().y(), &obj))
    {
      CObjFillInfo info;
      ofiItem_t    item;

      info.fillInfo(&m_mapView, &obj, &item);
      pcMainWnd->fillQuickInfo(&item);
    }
    m_bClick = false;
  }

  if (m_bZoomByMouse)
  { // zoom map    
    QRect  rc(m_zoomPoint, m_lastMousePos);
    double x, y;

    double fov = calcNewPos(&rc, &x, &y);
    if (fov != 0)
    {
      centerMap(x, y, fov);
    }
    m_bZoomByMouse = false;
  }

  repaintMap(true);
}

// CONTROL::
// Shift : pomale
// CTRL  : rychle
// MOUSE WHEEL = zoom
// CURSOR KEYS = posun

// CTRL + dblclick = centrovani a zoom
// MIDDLE MOUSE + tahani = posun mapy
// SPACE = centrovani mereni
// INSERT, PG UP = rotace
// HOME = resetovani rotace

////////////////////////////////////////////////////
void CMapView::mouseDoubleClickEvent(QMouseEvent *e)
////////////////////////////////////////////////////
{
  double x, y;

  //qDebug("dbl");

  m_bClick = false;
  m_bMouseMoveMap = false;
  m_bZoomByMouse = false;

  if ((e->buttons() & Qt::LeftButton) == Qt::LeftButton && (e->modifiers() & Qt::ControlModifier))
  {
    trfConvScrPtToXY(m_lastMousePos.x(), m_lastMousePos.y(), x, y);
    centerMap(x, y, m_mapView.fov * 0.5);
  }
}


////////////////////////////////////////////////////////////
double CMapView::calcNewPos(QRect *rc, double *x, double *y)
////////////////////////////////////////////////////////////
{
  double RA1,DEC1;
  double RA2,DEC2;
  double fov, cx, cy;

  if (abs(rc->width()) < 16 || abs(rc->height()) < 16)
  {
    return(0);
  }

  trfConvScrPtToXY(rc->left(), rc->top(), RA1, DEC1);
  trfConvScrPtToXY(rc->right(), rc->bottom(), RA2, DEC2);
  fov = anSep(RA1, DEC1, RA2, DEC2);

  if (fov < MIN_MAP_FOV)
    fov = MIN_MAP_FOV;
  if (fov > MAX_MAP_FOV)
    fov = MAX_MAP_FOV;

  cx = rc->left() + ((rc->right() - rc->left()) / 2.);
  cy = rc->top() + ((rc->bottom() - rc->top()) / 2.);
  trfConvScrPtToXY(cx, cy, RA2, DEC2);

  // todo : opravit o refrakci (mozna???) KONTROLA!!!!!
  *x = RA2;
  *y = DEC2;

  return(fov);
}

///////////////////////////////////////////////////////
void CMapView::keyEvent(int key, Qt::KeyboardModifiers)
///////////////////////////////////////////////////////
{
  double mul = 1;

  if (QApplication::keyboardModifiers() & Qt::ShiftModifier)
  {
    mul = 0.1;
  }

  if (QApplication::keyboardModifiers() & Qt::AltModifier)
  {
    double fov = 10;
    for (int k = Qt::Key_1; k <= Qt::Key_9; k++, fov += 10)
    {
      if (key == k)
      {
        m_mapView.fov = D2R(fov);
        repaintMap();
        return;
      }
    }
  }

  if (bConstEdit)
  {
    if (key == Qt::Key_T)
    {
      dev_const_type++;
      if (dev_const_type == 3)
        dev_const_type = 1;

      repaintMap(true);
      return;
    }

    if (key == Qt::Key_Space)
    {
      radec_t rd;
      constelLine_t c;

      if (tConstLines.count() == 0)
        return;

      if (!mapObjSnap(m_lastMousePos.x(), m_lastMousePos.y(), &rd))
      {
        trfConvScrPtToXY(m_lastMousePos.x(), m_lastMousePos.y(), rd.Ra, rd.Dec);
      }
      c.cmd = dev_const_type;
      c.pt = rd;

      tConstLines.append(c);
      dev_const_sel = tConstLines.count() - 1;

      repaintMap(false);
      return;
    }
  }

  if (bDevelopMode && dev_move_index == -1)
  {
    // new shape
    if (key == Qt::Key_N)
    {
      dev_shape_t s;
      radec_t     rd;

      s.name = "Unnamed";
      s.typeId = 0;

      QPoint pt = mapFromGlobal(cursor().pos());

      trfConvScrPtToXY(pt.x(), pt.y(), rd.Ra, rd.Dec);
      s.shapes.append(rd);

      trfConvScrPtToXY(pt.x() + 50, pt.y(), rd.Ra, rd.Dec);
      s.shapes.append(rd);

      trfConvScrPtToXY(pt.x() + 50, pt.y() + 50, rd.Ra, rd.Dec);
      s.shapes.append(rd);

      trfConvScrPtToXY(pt.x(), pt.y() + 50, rd.Ra, rd.Dec);
      s.shapes.append(rd);

      dev_shape_index = dev_shapes.count();

      dev_shapes.append(s);

      repaintMap(true);
      return;
    }
    else // new vertex
    if (key == Qt::Key_V && dev_shape_index != -1)
    {
      radec_t rd;

      trfConvScrPtToXY(m_lastMousePos.x(), m_lastMousePos.y(), rd.Ra, rd.Dec);
      dev_shapes[dev_shape_index].shapes.append(rd);
      repaintMap(true);
      return;
    }
    else // delete vertex
    if (key == Qt::Key_Backspace && dev_shape_index != -1)
    {
      if (dev_shapes[dev_shape_index].shapes.count() > 3)
      {
        dev_shapes[dev_shape_index].shapes.removeLast();
      }
      repaintMap(true);
      return;
    }
    else  //delete all shapes
    if (key == Qt::Key_Delete && (QApplication::keyboardModifiers() & Qt::CTRL))
    {
      for (int i = 0; i < dev_shapes.count(); i++)
      {
        dev_shapes[i].shapes.clear();
      }
      dev_shapes.clear();
      dev_shape_index = -1;

      repaintMap(true);
      return;
    }
    else  //delete shape
    if (key == Qt::Key_Delete && dev_shape_index != -1)
    {
      dev_shapes.removeAt(dev_shape_index);

      dev_shape_index--;
      if (dev_shape_index == -1)
      {
        dev_shape_index = dev_shapes.count() - 1;
      }
      repaintMap(true);
      return;
    } // next/prev in shape
    else
    if (key == Qt::Key_W && dev_shape_index != -1)
    {
      if (++dev_shape_index >= dev_shapes.count())
        dev_shape_index = 0;

      centerMap(dev_shapes[dev_shape_index].shapes.last().Ra, dev_shapes[dev_shape_index].shapes.last().Dec);

      repaintMap(true);
      return;
    }
    else
    if (key == Qt::Key_Q && dev_shape_index != -1)
    {
      if (--dev_shape_index < 0)
        dev_shape_index = dev_shapes.count() - 1;

      centerMap(dev_shapes[dev_shape_index].shapes.last().Ra, dev_shapes[dev_shape_index].shapes.last().Dec);

      repaintMap(true);
      return;
    }
    else // rotate CW
    if (key == Qt::Key_2 && dev_shape_index != -1)
    {
      dev_shapes[dev_shape_index].shapes.append(dev_shapes[dev_shape_index].shapes.first());
      dev_shapes[dev_shape_index].shapes.removeFirst();
      repaintMap(true);
      return;
    }

    // rotate CCW
    if (key == Qt::Key_1 && dev_shape_index != -1)
    {
      dev_shapes[dev_shape_index].shapes.insert(0, dev_shapes[dev_shape_index].shapes.last());
      dev_shapes[dev_shape_index].shapes.removeLast();
      repaintMap(true);
      return;
    }

    // change ID
    if (key == Qt::Key_BracketLeft && dev_shape_index != -1)
    {
      dev_shapes[dev_shape_index].typeId--;
      repaintMap(true);
      return;
    }

    if (key == Qt::Key_BracketRight && dev_shape_index != -1)
    {
      dev_shapes[dev_shape_index].typeId++;
      repaintMap(true);
      return;
    }
  }

  if (key == Qt::Key_Plus)
  {
    addFov(1, mul);
    repaintMap();
  }
  else
  if (key == Qt::Key_Minus)
  {
    addFov(-1, mul);
    repaintMap();
  }
  else
  if (key == Qt::Key_Left)
  {
    addX(1, mul);
    repaintMap();
  }
  else
  if (key == Qt::Key_Right)
  {
    addX(-1, mul);
    repaintMap();
  }
  else
  if (key == Qt::Key_Up)
  {
    addY(1, mul);
    repaintMap();
  }
  else
  if (key == Qt::Key_Down)
  {
    addY(-1, mul);
    repaintMap();
  }
  else
  if (key == Qt::Key_Insert)
  {
    m_mapView.roll += D2R(5) * mul;
    repaintMap();
  }
  else
  if (key == Qt::Key_PageUp)
  {
    m_mapView.roll -= D2R(5) * mul;
    repaintMap();
  }
  else
  if (key == Qt::Key_Home)
  {
    m_mapView.roll = 0;
    repaintMap();
  }

  if (key == Qt::Key_Space)
  { // move measure point
    trfConvScrPtToXY(m_lastMousePos.x(), m_lastMousePos.y(), m_measurePoint.Ra, m_measurePoint.Dec);
    repaintMap(false);
  }

  if (key == Qt::Key_Enter || key == Qt::Key_Return)
  {
    g_cDrawing.done();
    repaintMap(true);
  }

  if (key == Qt::Key_Escape)
  {
    g_cDrawing.cancel();
    repaintMap(true);
  }
}


////////////////////////////
void CMapView::saveSetting()
////////////////////////////
{
  QSettings settings;

  settings.setValue("geo/longitude", m_mapView.geo.lon);
  settings.setValue("geo/latitude", m_mapView.geo.lat);
  settings.setValue("geo/altitude", m_mapView.geo.alt);
  settings.setValue("geo/tzo", m_mapView.geo.tzo);
  settings.setValue("geo/sdlt", m_mapView.geo.sdlt);
  settings.setValue("geo/temp", m_mapView.geo.temp);
  settings.setValue("geo/press", m_mapView.geo.press);
  settings.setValue("geo/name", m_mapView.geo.name);

  settings.setValue("delta_t/delta_t", m_mapView.deltaT);
  settings.setValue("delta_t/delta_t_alg", m_mapView.deltaTAlg);

  settings.setValue("saving/drawing", g_autoSave.drawing);
  settings.setValue("saving/events", g_autoSave.events);
  settings.setValue("saving/tracking", g_autoSave.tracking);
}

/////////////////////////////////
void CMapView::gotoMeasurePoint()
/////////////////////////////////
{
  centerMap(m_measurePoint.Ra, m_measurePoint.Dec, CM_UNDEF);
}


///////////////////////////////////////////////////////////
void CMapView::centerMap(double ra, double dec, double fov)
///////////////////////////////////////////////////////////
{
  cAstro.setParam(&m_mapView);

  if (m_mapView.coordType == SMCT_ALT_AZM)
  {
     double alt, azm;

     // convert ra/de to alt/azm
     cAstro.convRD2AANoRef(ra, dec, &azm, &alt);

     if (ra != CM_UNDEF)
       ra = -azm;

     if (dec != CM_UNDEF)
       dec = alt;
  }
  else
  if (m_mapView.coordType == SMCT_ECL)
  {
    // convert ra/dec to ecl
    double lon, lat;

    cAstro.convRD2Ecl(ra, dec, &lon, &lat);

    if (ra != CM_UNDEF)
      ra = lon;

    if (dec != CM_UNDEF)
      dec = lat;
  }

  if (ra != CM_UNDEF)
    m_mapView.x = ra;

  if (dec != CM_UNDEF)
    m_mapView.y = dec;

  if ((fov != CM_UNDEF && !g_lockFOV) || (m_bZoomByMouse && fov != CM_UNDEF))
    m_mapView.fov = fov;

  m_mapView.fov = CLAMP(m_mapView.fov, MIN_MAP_FOV, MAX_MAP_FOV);
  rangeDbl(&m_mapView.x, R360);
  m_mapView.y = CLAMP(m_mapView.y, -R90, R90);

  repaintMap(true);
}


//////////////////////////////////////
void CMapView::changeMapView(int type)
//////////////////////////////////////
{
  int prev = m_mapView.coordType;

  if (type == prev)
    return;

  double x, y;
  trfConvScrPtToXY(width() / 2., height() / 2.0, x, y);

  m_mapView.coordType = type;
  centerMap(x, y, CM_UNDEF);
}


////////////////////////////////////////////
double CMapView::getStarMagnitudeLevel(void)
////////////////////////////////////////////
{
  int i;
  double mag = 1;

  if (m_magLock)
    return(m_lastStarMag);

  for (i = 0; i < MAG_RNG_COUNT; i++)
  {
    if (m_mapView.fov <= g_skSet.map.starRange[i].fromFov)
    {
      mag = g_skSet.map.starRange[i].mag;
    }
  }

  if (mag != m_lastStarMag)
  {
    m_mapView.starMagAdd = 0;
    m_lastStarMag = mag;
  }

  return(mag);
}


///////////////////////////////////////////
double CMapView::getDsoMagnitudeLevel(void)
///////////////////////////////////////////
{
  int i;
  double mag = 1;

  if (m_magLock)
    return(m_lastDsoMag);

  for (i = 0; i < MAG_RNG_COUNT; i++)
  {
    if (m_mapView.fov <= g_skSet.map.dsoRange[i].fromFov)
    {
      mag = g_skSet.map.dsoRange[i].mag;
    }
  }

  if (mag != m_lastDsoMag)
  {
    m_mapView.dsoMagAdd = 0;
    m_lastDsoMag = mag;
  }

  return(mag);
}

//////////////////////////////////////////////////
void CMapView::slotCheckedMagLevLock(bool checked)
//////////////////////////////////////////////////
{
  m_magLock = checked;
  repaintMap();
}

/////////////////////////////////////////////
void CMapView::slotCheckedFlipX(bool checked)
/////////////////////////////////////////////
{
  m_mapView.flipX = checked;
  repaintMap();
}


/////////////////////////////////////////////
void CMapView::slotCheckedFlipY(bool checked)
/////////////////////////////////////////////
{
  m_mapView.flipY = checked;
  repaintMap();
}

////////////////////////////////////////////////////////
void CMapView::slotTelePlugChange(double ra, double dec)
////////////////////////////////////////////////////////
{
  m_lastTeleRaDec.Ra = D2R(ra * 15);
  m_lastTeleRaDec.Dec = D2R(dec);

  recenterHoldObject(this, false);
  repaintMap();
}

///////////////////////////////////
void CMapView::slotZoom(float zoom)
///////////////////////////////////
{
  if (zoom < 0)
  {
    double step = (m_mapView.fov * 0.9) - m_mapView.fov;
    m_mapView.fov += step * fabs(zoom);
  }
  else
  {
    double step = m_mapView.fov - (m_mapView.fov * 1.1);
    m_mapView.fov -= step * fabs(zoom);
  }

  m_mapView.fov = CLAMP(m_mapView.fov, MIN_MAP_FOV, MAX_MAP_FOV);
  repaintMap();
}

/////////////////////////////////////////////
void CMapView::addFov(double dir, double mul)
/////////////////////////////////////////////
{
  if (dir >= 1)
  {
    double step = (m_mapView.fov * 0.8) - m_mapView.fov;
    m_mapView.fov += step * mul;
  }
  else
  {
    double step = m_mapView.fov - (m_mapView.fov * 1.2);
    m_mapView.fov -= step * mul;
  }

  m_mapView.fov = CLAMP(m_mapView.fov, MIN_MAP_FOV, MAX_MAP_FOV);
}


///////////////////////////////////////////
void CMapView::addX(double dir, double mul)
///////////////////////////////////////////
{
  double asp = width() / (double)height();

  if (m_mapView.flipX)
  {
    if (dir == -1)
      dir = 1;
    else
      dir = -1;
  }

  // TODO: udelat to poradne (posun mapy)

  //qDebug("f = %f", LERP(sin(fabs(m_mapView.y)), m_mapView.fov, MAX_MAP_FOV));

  m_mapView.x += dir * LERP(sin(fabs(m_mapView.y)), m_mapView.fov, MAX_MAP_FOV) * 0.05 * asp * mul;
  rangeDbl(&m_mapView.x, R360);
}


///////////////////////////////////////////
void CMapView::addY(double dir, double mul)
///////////////////////////////////////////
{
  if (m_mapView.flipY)
  {
    if (dir == -1)
      dir = 1;
    else
      dir = -1;
  }

  m_mapView.y += dir * m_mapView.fov * 0.05 * mul;
  m_mapView.y = CLAMP(m_mapView.y, -R90, R90);
}

//////////////////////////////
void CMapView::addX(double val)
//////////////////////////////
{
  double asp = width() / (double)height();

  // TODO: udelat to poradne
  m_mapView.x += val * asp;
  rangeDbl(&m_mapView.x, R360);
}


///////////////////////////////
void CMapView::addY(double val)
///////////////////////////////
{
  m_mapView.y += val;
  m_mapView.y = CLAMP(m_mapView.y, -R90, R90);
}


//////////////////////////////////
void CMapView::addStarMag(int dir)
//////////////////////////////////
{
  m_mapView.starMagAdd += dir * 0.5;
}


/////////////////////////////////
void CMapView::addDsoMag(int dir)
/////////////////////////////////
{
  m_mapView.dsoMagAdd += dir * 0.5;
}



////////////////////////////////////
void CMapView::updateStatusBar(void)
////////////////////////////////////
{
  double ra, dec;
  double azm, alt;

  trfConvScrPtToXY(m_lastMousePos.x(), m_lastMousePos.y(), ra, dec);
  cAstro.convRD2AARef(ra, dec, &azm, &alt);

  if (pcMainWnd->statusBar)
  {
    pcMainWnd->statusBar->setItem(SB_SM_CONST,QString("%1").arg(constGetName(constWhatConstel(ra, dec, m_mapView.jd), 1)));
    pcMainWnd->statusBar->setItem(SB_SM_RA,   QString(tr("R.A. : %1")).arg(getStrRA(ra)));
    pcMainWnd->statusBar->setItem(SB_SM_DEC,  QString(tr("Dec. : %1")).arg(getStrDeg(dec)));
    pcMainWnd->statusBar->setItem(SB_SM_FOV,  QString("FOV : %1").arg(getStrDeg(m_mapView.fov)));
    pcMainWnd->statusBar->setItem(SB_SM_MAGS, QString("Star : %1 mag. / DSO %2 mag.").arg(m_mapView.starMag, 0, 'f', 1).arg(m_mapView.dsoMag, 0, 'f', 1));

    pcMainWnd->statusBar->setItem(SB_SM_ALT,   QString(tr("Alt. : %1")).arg(getStrDeg(alt)));
    pcMainWnd->statusBar->setItem(SB_SM_AZM,  QString(tr("Azm. : %1")).arg(getStrDeg(azm)));

    pcMainWnd->statusBar->setItem(SB_SM_DATE,  QString("Date : %1").arg(getStrDate(m_mapView.jd, m_mapView.geo.tz)));
    pcMainWnd->statusBar->setItem(SB_SM_TIME,  QString("Time : %1").arg(getStrTime(m_mapView.jd, m_mapView.geo.tz)));

    double sep = anSep(m_measurePoint.Ra, m_measurePoint.Dec, ra, dec);
    double ang = RAD2DEG(trfGetPosAngle(ra, dec, m_measurePoint.Ra, m_measurePoint.Dec));
    pcMainWnd->statusBar->setItem(SB_SM_MEASURE, QString(tr("Sep : %1 / PA : %2°")).arg(getStrDegNoSign(sep)).arg(ang, 0, 'f', 2));
  }

  if (bDevelopMode)
  {
    QString str;

    str += "Shapes cnt. : " + QString("%1").arg(dev_shapes.count()) + "\n";
    if (dev_shape_index != -1)
    {
      //str += "  Shape name : " + dev_shapes[dev_shape_index].name + "\n";
      str += "  Vertices cnt. : " + QString("%1").arg(dev_shapes[dev_shape_index].shapes.count()) + "\n";
      str += "  Shape ID : " + QString("%1").arg(dev_shapes[dev_shape_index].typeId) + "\n";
    }
    pcMainWnd->setShapeInfo(str);
  }


  /*
  cMainWnd->statusBar->setItem(SB_SM_RA,  QString("R.A. : %1").arg(getStrRA(ra)));
  cMainWnd->statusBar->setItem(SB_SM_DEC, QString("Dec : %1").arg(getStrDeg(dec)));
  cMainWnd->statusBar->setItem(SB_SM_AZM,  QString("Azm. : %1").arg(getStrDeg(azm)));
  cMainWnd->statusBar->setItem(SB_SM_ALT, QString("Alt : %1").arg(getStrDeg(alt)));
  cMainWnd->statusBar->setItem(SB_SM_MAGS, QString("Star : %1 mag. / DSO %2 mag.").arg(mapView.starMag).arg(mapView.dsoMag));
  cMainWnd->statusBar->setItem(SB_SM_FOV, QString("FOV : %1").arg(getStrDeg(mapView.fov)));
  cMainWnd->statusBar->setItem(SB_SM_DATE,  QString("Date : %1").arg(getStrDate(mapView.jd, mapView.geo.timeZone + mapView.geo.sdlt)));
  cMainWnd->statusBar->setItem(SB_SM_TIME,  QString("Time : %1").arg(getStrTime(mapView.jd, mapView.geo.timeZone + mapView.geo.sdlt)));
  cMainWnd->statusBar->setItem(SB_SM_CONST,  QString("%1").arg(smGetConstelLongName(smWhatConstel(ra, dec, mapView.jd))));

  double sep = anSep(measureRD[0], measureRD[1], ra, dec);
  double ang = RAD2DEG(getPosAngle(ra, dec, measureRD[0], measureRD[1]));
  cMainWnd->statusBar->setItem(SB_SM_MEASURE, QString("%1 / %2Â°").arg(getStrDeg(sep)).arg(ang, 0, 'f', 2));
  */
}


////////////////////////////////////////////////////////////////
void CMapView::getMapRaDec(double &ra, double &dec, double &fov)
////////////////////////////////////////////////////////////////
{
  trfConvScrPtToXY(m_lastMousePos.x(), m_lastMousePos.y(), ra, dec);
  fov = m_mapView.fov;
}

//////////////////////////////
void CMapView::saveShape(void)
//////////////////////////////
{
  // save
  QString name = QFileDialog::getSaveFileName(this, tr("Save File"),
                             "dev_shapes/untitled.shp",
                             tr("Skytech dev. shapes (*.shp)"));

  if (name.isEmpty() || dev_shapes.count() == 0)
   return;

  SkFile f(name);
  QDataStream s(&f);

  if (f.open(SkFile::WriteOnly))
  {
   s << dev_shapes.count();
   for (int i = 0; i < dev_shapes.count(); i++)
   {
     s << dev_shapes[i].name;
     s << dev_shapes[i].typeId;
     s << dev_shapes[i].shapes.count();
     for (int j = 0; j < dev_shapes[i].shapes.count(); j++)
     {
       s << dev_shapes[i].shapes[j].Ra;
       s << dev_shapes[i].shapes[j].Dec;
     }
   }
  }
}

//////////////////////////////
void CMapView::loadShape(void)
//////////////////////////////
{
  // load
  QString name = QFileDialog::getOpenFileName(this, tr("Open File"),
                              "dev_shapes",
                              tr("Skytech dev. shapes (*.shp)"));
  if (name.isEmpty())
    return;

  // delete
  for (int i = 0; i < dev_shapes.count(); i++)
  {
    dev_shapes[i].shapes.clear();
  }
  dev_shapes.clear();

  SkFile f(name);
  QDataStream s(&f);
  int cnt, c;

  if (f.open(SkFile::ReadOnly))
  {
    s >> cnt;
    for (int i = 0; i < cnt; i++)
    {
      dev_shape_t shp;

      s >> shp.name;
      s >> shp.typeId;
      s >> c;

      dev_shapes.append(shp);

      for (int j = 0; j < c; j++)
      {
        radec_t rd;

        s >> rd.Ra;
        s >> rd.Dec;

        dev_shapes[i].shapes.append(rd);
      }
    }
  }
  if (dev_shapes.count() > 0)
    dev_shape_index = 0;
  else
    dev_shape_index = -1;

  repaintMap(true);
}

/////////////////////////////////////////////////////
bool CMapView::isRaDecOnScreen(double ra, double dec)
/////////////////////////////////////////////////////
{
  SKPOINT pt;
  radec_t rd;

  rd.Ra = ra;
  rd.Dec = dec;

  trfRaDecToPointNoCorrect(&rd, &pt);
  if (trfProjectPoint(&pt))
    return(true);

  return(false);
}

/////////////////////////
void CMapView::printMap()
/////////////////////////
{
  setting_t currentSetting = g_skSet;
  bool bw;

  CGetProfile dlgProfile;

  if (dlgProfile.exec() == DL_CANCEL)
    return;

  QPrinter prn(QPrinter::ScreenResolution);
  QPrintDialog dlg(&prn, this);

  prn.setPageMargins(15, 15, 15, 15, QPrinter::Millimeter);

  if (dlg.exec() == DL_CANCEL)
  {
    return;
  }

  if (dlgProfile.m_name.compare("$BLACKWHITE$"))
  {
    setLoad(dlgProfile.m_name);
    bw = false;
  }
  else
  {
    bw = true;
  }

  CSkPainter p;
  p.begin(&prn);

  QImage *img = new QImage(p.device()->width(), p.device()->height(), QImage::Format_ARGB32_Premultiplied);

  CSkPainter p1;

  p1.begin(img);

  p1.setRenderHint(QPainter::Antialiasing, true);
  p1.setRenderHint(QPainter::SmoothPixmapTransform, true);

  if (bw)
  {
    setPrintConfig();
  }

  m_mapView.starMag = m_mapView.starMagAdd + getStarMagnitudeLevel();
  m_mapView.dsoMag = m_mapView.dsoMagAdd + getDsoMagnitudeLevel();
  smRenderSkyMap(&m_mapView, &p1, img);

  p1.end();

  p.drawImage(0, 0, *img);

  p.setPen(Qt::black);
  p.setBrush(Qt::NoBrush);
  p.drawRect(0, 0, p.device()->width() - 1, p.device()->height() - 1);
  p.end();

  delete img;

  if (bw)
  {
    restoreFromPrintConfig();
  }
  //g_skSet = currentSetting;
  //setCreateFonts();
}


////////////////////////////////////////
void CMapView::repaintMap(bool bRepaint)
////////////////////////////////////////
{
  if (!m_bInit)
    return;

  m_mapView.jd = CLAMP(m_mapView.jd, MIN_JD, MAX_JD);
  m_mapView.roll = CLAMP(m_mapView.roll, D2R(-90), D2R(90));

  pcMainWnd->timeDialogUpdate();

  if (bRepaint)
  {
    CSkPainter p(pBmp);

    if (pcMainWnd->isQuickInfoTimeUpdate())
    {
      CObjFillInfo info;
      ofiItem_t    *item = pcMainWnd->getQuickInfo();
      ofiItem_t    newItem;

      if (item && !equals(m_mapView.jd, item->jd))
      {
        info.fillInfo(&m_mapView, &item->mapObj, &newItem);
        pcMainWnd->fillQuickInfo(&newItem, true);
      }
    }

    timer.start();

    g_cDrawing.setView(&m_mapView);
    m_mapView.starMag = m_mapView.starMagAdd + getStarMagnitudeLevel();
    m_mapView.dsoMag = m_mapView.dsoMagAdd + getDsoMagnitudeLevel();
    smRenderSkyMap(&m_mapView, &p, pBmp);

    if (g_developMode)
    {
      p.setPen(QColor(255, 255, 255));
      p.setFont(QFont("arial", 12, 250));
      qint64 elp = timer.elapsed();
      p.drawText(10, 40, QString::number(elp) + "ms " + QString::number(1000 / (float)elp, 'f', 1) + " fps");
    }
  }

  updateStatusBar();
  QWidget::update();
}

//////////////////////////////////////
void CMapView::enableConstEditor(bool)
//////////////////////////////////////
{
  bConstEdit = !bConstEdit;

  if (m_mapView.jd != JD2000 && bConstEdit)
  {
    msgBoxInfo(this, tr("Setting time to epoch J2000.0"));
    m_mapView.jd = JD2000;
  }

  dev_move_index = -1;
  dev_shape_index = -1;

  repaintMap(true);
}


/////////////////////////////////////////////
void CMapView::enableShapeEditor(bool enable)
/////////////////////////////////////////////
{
  bDevelopMode = enable;

  if (m_mapView.jd != JD2000 && bDevelopMode)
  {
    msgBoxInfo(this, tr("Setting time to epoch J2000.0"));
    m_mapView.jd = JD2000;
  }

  dev_move_index = dev_shapes.count() - 1;
  dev_shape_index = dev_shapes.count() - 1;

  repaintMap(true);
}

////////////////////////////////
QImage *CMapView::getImage(void)
////////////////////////////////
{
  return(pBmp);
}

////////////////////////////////////////
void CMapView::paintEvent(QPaintEvent *)
////////////////////////////////////////
{
  CSkPainter p(this);

  if (!m_bInit)
    return;

  if (g_antialiasing)
  {
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
  }
  else
  {
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
  }

  p.drawImage(0, 0, *pBmp);
  g_cDrawing.setView(&m_mapView);

  ofiItem_t *info = pcMainWnd->getQuickInfo();
  if (info != NULL)
  {
    SKPOINT pt;

    trfRaDecToPointNoCorrect(&info->radec, &pt);
    if (trfProjectPoint(&pt))
    {
      p.setPen(QPen(QColor(g_skSet.map.objSelectionColor), 3));
      p.drawCornerBox(pt.sx, pt.sy, 10, 4);
    }
  }

  if (m_bZoomByMouse)
  {
    QRect  rc(m_zoomPoint, m_lastMousePos);
    double x, y;

    double fov = calcNewPos(&rc, &x, &y);

    p.setBrush(Qt::NoBrush);

    if (fov != 0)
    {
      p.setPen(QPen(QColor(255, 255, 255), 3, Qt::DotLine));
    }
    else
    {
      p.setPen(QPen(QColor(255, 0, 0), 3, Qt::DotLine));
    }

    p.setCompositionMode(QPainter::CompositionMode_Difference);
    p.drawRect(QRect(m_zoomPoint, m_lastMousePos));
    int mm = qMin(m_lastMousePos.x() - m_zoomPoint.x(), m_lastMousePos.y() - m_zoomPoint.y());
    int size = qMax((int)(mm * 0.05f), 5);
    p.setPen(QPen(QColor(255, 255, 255), 1, Qt::SolidLine));
    p.drawCross(m_zoomPoint + (m_lastMousePos - m_zoomPoint) * 0.5, size);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
  }

  if (g_dssUse)
  { // show download rectangle
    p.setPen(QPen(QColor(g_skSet.map.drawing.color), 3, Qt::DotLine));
    p.setBrush(Qt::NoBrush);

    if (g_ddsSimpleRect)
    {
      int r = (int)(p.device()->width() / RAD2DEG(m_mapView.fov) * (g_dssSize / 60.0) / 2.0);
      radec_t   rd;
      SKPOINT   pt;

      rd.Ra = g_dssRa;
      rd.Dec = g_dssDec;

      trfRaDecToPointCorrectFromTo(&rd, &pt, m_mapView.jd, JD2000);
      if (trfProjectPoint(&pt))
      {
        QRect rc;

        p.save();

        p.translate(pt.sx, pt.sy);
        p.rotate(180 - R2D(trfGetAngleToNPole(rd.Ra, rd.Dec)));
        p.translate(-pt.sx, -pt.sy);

        rc.setX(pt.sx - r);
        rc.setY(pt.sy - r);
        rc.setWidth(r * 2);
        rc.setHeight(r * 2);

        p.drawRect(rc);

        p.restore();
      }
    }
    else
    {
      SKPOINT pt[4];

      for (int i = 0; i < 4; i++)
      {
        trfRaDecToPointNoCorrect(&g_dssCorner[i], &pt[i]);
      }

      if (!SKPLANECheckFrustumToPolygon(trfGetFrustum(), pt, 4))
        return;

      for (int i = 0; i < 4; i++)
      {
        trfProjectPointNoCheck(&pt[i]);
      }

      p.setPen(g_skSet.map.drawing.color);
      p.drawLine(pt[0].sx, pt[0].sy, pt[1].sx, pt[1].sy);
      p.drawLine(pt[1].sx, pt[1].sy, pt[2].sx, pt[2].sy);
      p.drawLine(pt[2].sx, pt[2].sy, pt[3].sx, pt[3].sy);
      p.drawLine(pt[3].sx, pt[3].sy, pt[0].sx, pt[0].sy);
    }
  }

  if (1)
  {
    SKPOINT pt;

    trfRaDecToPointCorrectFromTo(&m_measurePoint, &pt, m_mapView.jd, JD2000);
    if (trfProjectPoint(&pt))
    { // draw meassure cross
      p.setPen(QPen(QBrush(g_skSet.map.measurePoint.color), g_skSet.map.measurePoint.width, (Qt::PenStyle)g_skSet.map.measurePoint.style));
      p.drawCross(pt.sx, pt.sy, 10);
    }

  }
  g_cDrawing.drawEditedObject(&p);


  if (bDevelopMode)
  {
    p.setBrush(QColor(0,0,0));

    for (int i = 0; i < dev_shapes.count(); i++)
    {
      dev_shape_t shape = dev_shapes[i];

      if (i == dev_shape_index)
        p.setPen(QPen(QColor(255, 0, 0), 2));
      else
        p.setPen(QPen(QColor(255, 255, 0), 2));

      for (int j = 0; j < shape.shapes.count(); j++)
      {
        radec_t r1 = shape.shapes[j];
        radec_t r2 = shape.shapes[(j + 1) % shape.shapes.count()];
        SKPOINT p1, p2;

        trfRaDecToPointNoCorrect(&r1, &p1);
        trfRaDecToPointNoCorrect(&r2, &p2);

        if (trfProjectLine(&p1, &p2))
        {
          if (j + 1 == shape.shapes.count() && (i == dev_shape_index))
          {
            p.setPen(QPen(QColor(200, 128, 128), 5));
            p.drawEllipse(QPoint(p1.sx, p1.sy), 5, 5);
            p.setPen(QPen(QColor(200, 128, 128), 1, Qt::DotLine));
          }
          p.drawLine(p1.sx, p1.sy, p2.sx, p2.sy);

          //if (i == dev_shape_index)
          p.drawEllipse(QPoint(p1.sx, p1.sy), 3, 3);
        }
      }
    }
  }

  if (bConstEdit)
    constRenderConstelationLines2Edit(&p, &m_mapView);

  // show icon on map (right-up) //////////////////////

  int iy = 10;

  if (pcMainWnd->m_bRealTime)
  {
    QPixmap pix(":/res/realtime.png");

    p.drawPixmap(width() - 10 - pix.width(), iy, pix);
    iy += pix.height() + 10;
  }

  if (pcMainWnd->m_bRealTimeLapse)
  {
    QPixmap pix(":/res/timelapse.png");

    p.drawPixmap(width() - 10 - pix.width(), iy, pix);
    iy += pix.height() + 10;
  }

  if (g_bHoldObject)
  {
    QPixmap pix(":/res/holdobj.png");

    p.drawPixmap(width() - 10 - pix.width(), iy, pix);
    iy += pix.height() + 10;
  }

  /////////////////////////////////////////////////////

  if (!helpText.isEmpty())
  { //show help
    p.setFont(QFont("Arial", 12, QFont::Bold));
    QFontMetrics fm(p.font());

    QRect rc;

    rc = fm.boundingRect(0, 0, 2000, 2000, Qt::AlignLeft, helpText);
    rc.moveTo(10, 10);
    rc.adjust(0, 0, 10, 10);
    rc.adjust(-5, -5, 5, 5);

    p.setBrush(Qt::black);
    p.setPen(Qt::white);
    p.setOpacity(0.8);
    p.drawRoundedRect(rc, 3, 3);
    p.setOpacity(1);

    //rc.adjust(-5, -5, 5, 5);
    rc.moveTo(15, 15);
    p.drawText(rc, Qt::AlignLeft, helpText);
  }
}

void CMapView::slotAnimChanged(curvePoint_t &p)
{
  qDebug() << p.x << p.y;

  centerMap(p.x, p.y, p.fov);
}
