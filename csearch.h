#ifndef CSEARCH_H
#define CSEARCH_H

#include "skcore.h"
#include "cmapview.h"
#include "castro.h"
#include "transform.h"
#include "cdso.h"
#include "constellation.h"
#include "tycho.h"
#include "mapobj.h"

class CSearch
{
public:
  CSearch();
  static bool search(mapView_t *mapView, QString str, double &ra, double &dec, double &fov, mapObj_t &obj);
};

#endif // CSEARCH_H
