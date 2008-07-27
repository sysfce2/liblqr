/* LiquidRescaling Library
 * Copyright (C) 2007 Carlo Baldassi (the "Author") <carlobaldassi@gmail.com>.
 * All Rights Reserved.
 *
 * This library implements the algorithm described in the paper
 * "Seam Carving for Content-Aware Image Resizing"
 * by Shai Avidan and Ariel Shamir
 * which can be found at http://www.faculty.idc.ac.il/arik/imret.pdf
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; version 3 dated June, 2007.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/> 
 */

#include <glib.h>
#include <math.h>
#include <lqr/lqr_base.h>
#include <lqr/lqr_gradient.h>
#include <lqr/lqr_progress_pub.h>
#include <lqr/lqr_cursor_pub.h>
#include <lqr/lqr_vmap_pub.h>
#include <lqr/lqr_vmap_list_pub.h>
#include <lqr/lqr_carver_list_pub.h>
#include <lqr/lqr_energy_pub.h>
#include <lqr/lqr_carver_pub.h>
#include <lqr/lqr_energy.h>

/* read average pixel value at x, y 
 * for energy computation */
inline gdouble
lqr_carver_read_brightness (LqrCarver * r, gint x, gint y)
{
  gdouble sum = 0;
  gint k;
  gchar has_alpha = (r->alpha_channel > 0 ? 1 : 0);
  gint now = r->raw[y][x];
  for (k = 0; k < r->channels; k++) if (k != r->alpha_channel)
    {
      sum += R_RGB(r->rgb, now * r->channels + k);
    }
  if (has_alpha)
    {
      sum *= R_RGB(r->rgb, now * r->channels + r->alpha_channel) / R_RGB_MAX;
    }
  return sum / (R_RGB_MAX * (r->channels - has_alpha));
}

inline gdouble
lqr_carver_read_luma (LqrCarver * r, gint x, gint y)
{
  gdouble sum = 0;
  gint now = r->raw[y][x];
  sum = 0.2126 * R_RGB(r->rgb, now * r->channels + 0) +
        0.7152 * R_RGB(r->rgb, now * r->channels + 1) +
        0.0722 * R_RGB(r->rgb, now * r->channels + 2);

  if (r->image_type == LQR_RGBA_IMAGE)
    {
      sum *= R_RGB(r->rgb, now * r->channels + 3) / R_RGB_MAX;
    }

  return sum / (R_RGB_MAX);
}

inline gdouble
lqr_carver_read_brightness_abs (LqrCarver * r, gint x1, gint y1, gint x2, gint y2)
{
  gchar has_alpha = (r->alpha_channel > 0 ? 1 : 0);
  gint p1, p2;
  gdouble a1, a2;
  gint k;
  gdouble sum = 0;
  p1 = r->raw[y1][x1];
  p2 = r->raw[y2][x2];
  if (has_alpha)
    {
      a1 = R_RGB(r->rgb, p1 * r->channels + r->alpha_channel) / R_RGB_MAX;
      a2 = R_RGB(r->rgb, p2 * r->channels + r->alpha_channel) / R_RGB_MAX;
    }
  else
    {
      a1 = a2 = 1;
    }
  for (k = 0; k < r->channels; k++) if (k != r->alpha_channel)
    {
      sum += fabs(R_RGB(r->rgb, p1 * r->channels + 0) * a1 - R_RGB(r->rgb, p2 * r->channels + 0) * a2);
    }
  return sum / (R_RGB_MAX * (r->channels - has_alpha));
}

inline gdouble
lqr_carver_read_luma_abs (LqrCarver * r, gint x1, gint y1, gint x2, gint y2)
{
  gint p1, p2;
  gdouble a1, a2;
  p1 = r->raw[y1][x1];
  p2 = r->raw[y2][x2];
  if (r->image_type == LQR_RGBA_IMAGE)
    {
      a1 = R_RGB(r->rgb, p1 * r->channels + 3) / R_RGB_MAX;
      a2 = R_RGB(r->rgb, p2 * r->channels + 3) / R_RGB_MAX;
    }
  else
    {
      a1 = a2 = 1;
    }
  return (0.2126 * fabs(R_RGB(r->rgb, p1 * r->channels + 0) * a1 - R_RGB(r->rgb, p2 * r->channels + 0) * a2) +
          0.7152 * fabs(R_RGB(r->rgb, p1 * r->channels + 1) * a1 - R_RGB(r->rgb, p2 * r->channels + 1) * a2) +
          0.0722 * fabs(R_RGB(r->rgb, p1 * r->channels + 2) * a1 - R_RGB(r->rgb, p2 * r->channels + 2) * a2)) /
          R_RGB_MAX;
}


/* compute energy at x, y */
double
lqr_energy_std (LqrCarver * r, gint x, gint y)
{
  gdouble gx, gy;

  if (y == 0)
    {
      gy = (*(r->nrg->rf)) (r, x, y + 1) - (*(r->nrg->rf)) (r, x, y);
    }
  else if (y < r->h - 1)
    {
      gy =
        ((*(r->nrg->rf)) (r, x, y + 1) - (*(r->nrg->rf)) (r, x, y - 1)) / 2;
    }
  else
    {
      gy = (*(r->nrg->rf)) (r, x, y) - (*(r->nrg->rf)) (r, x, y - 1);
    }

  if (x == 0)
    {
      gx = (*(r->nrg->rf)) (r, x + 1, y) - (*(r->nrg->rf)) (r, x, y);
    }
  else if (x < r->w - 1)
    {
      gx =
        ((*(r->nrg->rf)) (r, x + 1, y) - (*(r->nrg->rf)) (r, x - 1, y)) / 2;
    }
  else
    {
      gx = (*(r->nrg->rf)) (r, x, y) - (*(r->nrg->rf)) (r, x - 1, y);
    }
  return (*(r->nrg->gf))(gx, gy);
}

double
lqr_energy_null (LqrCarver * r, gint x, gint y)
{
  return 0;
}

/* compute energy at x, y */
double
lqr_energy_abs (LqrCarver * r, gint x, gint y)
{
  gdouble gx, gy;

  if (y == 0)
    {
      gy = (*(r->nrg->rfabs))(r, x, y + 1, x, y);
    }
  else if (y < r->h - 1)
    {
      gy = 0.5 * (*(r->nrg->rfabs))(r, x, y + 1, x, y - 1);
    }
  else
    {
      gy = (*(r->nrg->rfabs))(r, x, y, x, y - 1);
    }

  if (x == 0)
    {
      gx = (*(r->nrg->rfabs))(r, x + 1, y, x, y);
    }
  else if (x < r->w - 1)
    {
      gx = 0.5 * (*(r->nrg->rfabs))(r, x + 1, y, x - 1, y);
    }
  else
    {
      gx = (*(r->nrg->rfabs))(r, x, y, x - 1, y);
    }
  return (*(r->nrg->gf))(gx, gy);
}

/* gradient function for energy computation */
LqrRetVal
lqr_carver_set_energy_function (LqrCarver * r, LqrEnergyFuncType ef_ind, LqrGradFuncType gf_ind, LqrReadFuncType rf_ind)
{
  switch (ef_ind)
    {
      case LQR_EF_STD:
        r->nrg->ef = &lqr_energy_std;
        switch (rf_ind)
          {
            case LQR_RF_BRIGHTNESS:
              r->nrg->rf = &lqr_carver_read_brightness;
              break;
            case LQR_RF_LUMA:
              switch (r->image_type)
              {
                case LQR_RGB_IMAGE:
                case LQR_RGBA_IMAGE:
                  r->nrg->rf = &lqr_carver_read_luma;
                  break;
                default:
                  r->nrg->rf = &lqr_carver_read_brightness;
                  break;
              }

              break;
            default:
              return LQR_ERROR;
          }
        break;
      case LQR_EF_ABS:
        r->nrg->ef = &lqr_energy_abs;
        switch (rf_ind)
          {
            case LQR_RF_BRIGHTNESS:
              r->nrg->rfabs = &lqr_carver_read_brightness_abs;
              break;
            case LQR_RF_LUMA:
              switch (r->image_type)
              {
                case LQR_RGB_IMAGE:
                case LQR_RGBA_IMAGE:
                  r->nrg->rfabs = &lqr_carver_read_luma_abs;
                  break;
                default:
                  r->nrg->rfabs = &lqr_carver_read_brightness_abs;
                  break;
              }

              break;
            default:
              return LQR_ERROR;
          }
        break;
      case LQR_EF_NULL:
        r->nrg->ef = &lqr_energy_null;
        return LQR_OK;
      default:
        return LQR_ERROR;
    }

  switch (gf_ind)
    {
    case LQR_GF_NORM:
      r->nrg->gf = &lqr_grad_norm;
      break;
    case LQR_GF_SUMABS:
      r->nrg->gf = &lqr_grad_sumabs;
      break;
    case LQR_GF_XABS:
      r->nrg->gf = &lqr_grad_xabs;
      break;
    case LQR_GF_NULL:
      r->nrg->gf = &lqr_grad_null;
    default:
      return LQR_ERROR;
    }

  return LQR_OK;
}
