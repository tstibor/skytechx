#ifndef CSEARCHDSOCAT_H
#define CSEARCHDSOCAT_H

#include "caddcustomobject.h"

#include <QDialog>

namespace Ui {
class CSearchDSOCat;
}

class CSearchDSOCat : public QDialog
{
  Q_OBJECT

public:
  explicit CSearchDSOCat(QWidget *parent = 0);
  ~CSearchDSOCat();
  double m_ra, m_dec, m_fov;

protected:
  void fillList();

private slots:
  void slotSelChange(QModelIndex &index);

  void on_cbCatalogue_currentIndexChanged(int index);

  void on_treeView_doubleClicked(const QModelIndex &index);

  void on_pushButton_2_clicked();

  void on_pushButton_clicked();

private:
  Ui::CSearchDSOCat *ui;
  QList <customCatalogue_t> m_catalogue;
};

#endif // CSEARCHDSOCAT_H
