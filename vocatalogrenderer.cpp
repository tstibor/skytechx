/***********************************************************************
This file is part of SkytechX.

Pavel Mraz, Copyright (C) 2016

SkytechX is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

SkytechX is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with SkytechX.  If not, see <http://www.gnu.org/licenses/>.
************************************************************************/

#include "vocatalogrenderer.h"
#include "skfile.h"
#include "transform.h"
#include "cstarrenderer.h"
#include "smartlabeling.h"
#include "cdso.h"
#include "mapobj.h"

#include <QDataStream>

void hammer(double mu, double phi, double &x, double &y)
{
  mu = qMin(+1., qMax(-1., mu));      // clamp mu to [-1,+1]
  double sintheta = sqrt(1 - mu*mu);  // sin(theta) = cos(latitude)
  double longitude = phi - M_PI;
  double z = sqrt(1 + sintheta*cos(longitude/2));
  x = -sintheta*sin(longitude/2)/z;
  y = mu/z;
}

inline static int previewOffset(double ra, double dec)
{
  double x;
  double y;

  ra += 180;
  while (ra >= 360) ra -= 360;
  while (ra <= -360) ra += 360;

  hammer(cos(D2R(dec + 90)), D2R(ra), x, y);

  x = ((1. + x ) * 0.5) * (double)VO_PREVIEW_SIZE_X;
  y = ((1. + y ) * 0.5) * (double)VO_PREVIEW_SIZE_Y;

  x = CLAMP(x, 0, VO_PREVIEW_SIZE_X - 1);
  y = CLAMP(y, 0, VO_PREVIEW_SIZE_Y - 1);

  return (int)x + ((int)y * VO_PREVIEW_SIZE_X);
}

inline static void previewXY(double ra, double dec, int &x, int &y)
{
  int offset = previewOffset(ra, dec);

  x = offset % VO_PREVIEW_SIZE_X;
  y = offset / VO_PREVIEW_SIZE_X;
}

VOCatalogRenderer::VOCatalogRenderer()
{  
  m_show = true;
}


bool VOCatalogRenderer::load(const QString &filePath)
{
  SkFile file(filePath + "/vo_data.dat");

  if (!file.open(QFile::ReadOnly))
  {
    return false;
  }

  m_path = filePath;

  QDataStream ds(&file);

  int *preview = new int[VO_PREVIEW_SIZE_Y * VO_PREVIEW_SIZE_X];

  memset(preview, 0, sizeof(int) * VO_PREVIEW_SIZE_Y * VO_PREVIEW_SIZE_X);
  int previewMax = 0;

  int count;

  ds >> count;
  ds >> m_show;
  ds >> m_type;
  ds >> m_desc;
  ds >> m_name;
  ds >> m_id;  

  m_brightestMag = 99;

  double raMin = 9999999;
  double raMax = -9999999;
  double decMin = 9999999;
  double decMax = -9999999;    

  for (int i = 0; i < count ; i++)
  {
    VOItem_t item;

    ds >> item.name;
    ds >> item.rd.Ra;
    ds >> item.rd.Dec;
    ds >> item.mag;
    ds >> item.axis[0];
    ds >> item.axis[1];
    ds >> item.pa;
    ds >> item.infoFileOffset;

    preview[previewOffset(item.rd.Ra, item.rd.Dec)]++;

    item.rd.Ra = D2R(item.rd.Ra);
    item.rd.Dec = D2R(item.rd.Dec);        

    if (item.rd.Ra < raMin) raMin = item.rd.Ra;
    if (item.rd.Ra > raMax) raMax = item.rd.Ra;
    if (item.rd.Dec < decMin) decMin = item.rd.Dec;
    if (item.rd.Dec > decMax) decMax = item.rd.Dec;

    if (item.mag < m_brightestMag)
    {
      m_brightestMag = item.mag;
    }

    //qDebug() << item.mag << item.axis[0] << item.axis[1] << item.rd.Ra << item.rd.Dec;

    m_data.append(item);
  }      

  // create bounding box
  trfRaDecToPointNoCorrect(&radec_t(raMin, decMin), &m_quad[0]);
  trfRaDecToPointNoCorrect(&radec_t(raMin, decMax), &m_quad[1]);
  trfRaDecToPointNoCorrect(&radec_t(raMax, decMin), &m_quad[2]);
  trfRaDecToPointNoCorrect(&radec_t(raMax, decMax), &m_quad[3]);

  QImage *image = new QImage(VO_PREVIEW_SIZE_X, VO_PREVIEW_SIZE_Y, QImage::Format_ARGB32);

  m_preview = QImage(*image);
  int *ptr = (int *)m_preview.bits();
  QImage mask = QImage(":/res/hammer_mask.png");

  for (int i = 0; i < VO_PREVIEW_SIZE_Y * VO_PREVIEW_SIZE_X; i++)
  {
    if (preview[i] > previewMax)
    {
      previewMax = preview[i];
    }
  }  

  QEasingCurve crv(QEasingCurve::OutQuint);

  for (int i = 0; i < VO_PREVIEW_SIZE_Y * VO_PREVIEW_SIZE_X; i++)
  {
    int val = crv.valueForProgress((preview[i] / (double)previewMax)) * 255.;
    *ptr = QColor(val, val, val).rgba();
    ptr++;
  }

  QPainter p(&m_preview);
  p.drawImage(0, 0, mask);
  p.end();

  delete image;
  m_preview.save(filePath + "/preview.png", "PNG");

  return true;
}

void VOCatalogRenderer::render(mapView_t *mapView, CSkPainter *pPainter)
{
  if (!m_show)
  {
    return;
  }  

  if (m_brightestMag > mapView->starMag)
  {
    //return;
  }

  if (!SKPLANECheckFrustumToPolygon(trfGetFrustum(), m_quad, 4))
  {
    //return;
  }

  cDSO.setPainter(pPainter, nullptr);

  float maxMag;

  if (m_type == DSOT_STAR)
  {
    maxMag = mapView->starMag;
  }
  else
  {
    maxMag = mapView->dsoMag;
  }

  int i = -1;
  foreach (const VOItem_t &item, m_data)
  {
    SKPOINT pt;

    i++;

    if (item.mag > maxMag)
    {
      continue;
    }

    //if (item.mag >= VO_INVALID_MAG && m_type )
    {
      //continue;
    }

    trfRaDecToPointNoCorrect(&item.rd, &pt);

    if (trfProjectPoint(&pt))
    {
      if (m_type != DSOT_STAR)
      {
        dso_t dso;

        dso.rd = item.rd;
        dso.mag = item.mag * 100;
        dso.sx = item.axis[0];
        dso.sy = item.axis[1];
        dso.pa = item.pa;
        dso.type = m_type;
        dso.shape = NO_DSO_SHAPE;
        dso.nameOffs = -1;

        cDSO.renderObj(&pt, &dso, mapView, false, 1);
        addMapObj(item.rd, pt.sx, pt.sy, MO_VOCATALOG, MO_CIRCLE, 5, (qint64)this, (qint64)&item, item.mag);
      }
      else
      {
        int r = cStarRenderer.renderStar(&pt, 0, item.mag, pPainter);
        addMapObj(item.rd, pt.sx, pt.sy, MO_VOCATALOG, MO_CIRCLE, r + 2, (qint64)this, (qint64)&item, item.mag);
        //g_labeling.addLabel(QPoint(pt.sx, pt.sy), 15, QString::number(item.mag), FONT_DRAWING, RT_BOTTOM_LEFT, SL_AL_ALL);
      }

      //g_labeling.addLabel(QPoint(pt.sx, pt.sy), 5, item.name, FONT_DRAWING, RT_BOTTOM_RIGHT, SL_AL_ALL);
      //g_labeling.addLabel(QPoint(pt.sx, pt.sy), 5, QString::number(item.pa), FONT_DRAWING, RT_BOTTOM_LEFT, SL_AL_ALL);
    }        
  }
}

void VOCatalogRenderer::setShow(bool show)
{
  m_show = show;

  SkFile file(m_path + "/vo_data.dat");

  if (!file.open(QFile::ReadWrite))
  {
    return;
  }

  file.seek(sizeof(int));
  file.write((char *)&show, sizeof(bool));
  file.close();
}

QList <VOTableItem_t> VOCatalogRenderer::getTableItem(VOItem_t &object)
{
  SkFile file(m_path + "/vo_table_data.bin");
  if (!file.open(QFile::ReadOnly))
  {
    return QList <VOTableItem_t>();
  }

  QList <VOTableItem_t> list;

  QDataStream ds(&file);

  VOTableItem_t item;

  int count;

  ds >> count;

  for (int i = 0; i < count; i++)
  {
    ds >> item.name;
    ds >> item.ucd;
    ds >> item.unit;
    ds >> item.desc;

    list.append(item);
  }

  file.seek(object.infoFileOffset);

  QStringList row;

  ds >> row;

  for (int i = 0; i < count; i++)
  {
    QString str = row[i];
    list[i].value = str;
  }

  return list;
}


