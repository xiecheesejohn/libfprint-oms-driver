/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Copyright (C) 2026 libfprint-oms-driver contributors
 * Modified for public source distribution on 2026-10-25.
 */

#pragma once

#include <glib.h>

/* OMSDeletChar deletes exactly one slot. Vendor DLL: FUN_1800070bc. */
static inline gsize
oms_build_delete (guint8 *out, guint16 slot)
{
  const guint8 header[] = {0xef, 1, 0xff, 0xff, 0xff, 0xff, 1, 0, 7, 0x0c};
  guint16 sum = 0;

  memcpy (out, header, sizeof header);
  out[10] = slot >> 8;
  out[11] = slot & 0xff;
  out[12] = 0;
  out[13] = 1;
  for (gsize i = 6; i < 14; i++)
    sum += out[i];
  out[14] = sum >> 8;
  out[15] = sum & 0xff;
  return 16;
}

/* LEN includes the two checksum bytes, but not the nine-byte header. */
static inline gboolean
oms_response_valid (const guint8 *rx, gsize size)
{
  guint16 len, sum = 0;

  if (size < 11 || rx[0] != 0xef || rx[1] != 0x01 || rx[6] != 0x07)
    return FALSE;
  len = ((guint16) rx[7] << 8) | rx[8];
  if (len < 2 || (gsize) len + 9 > size)
    return FALSE;
  for (gsize i = 6; i < (gsize) len + 7; i++)
    sum += rx[i];
  return sum == (((guint16) rx[len + 7] << 8) | rx[len + 8]);
}

static inline gboolean
oms_verify_touch (const guint8 *rx, gsize size)
{
  /* Live verify captures use status 1; status 3 belongs to enrollment. */
  return oms_response_valid (rx, size) && rx[7] == 0 && rx[8] == 8 &&
         rx[9] == 0 && rx[10] == 1;
}

static inline gboolean
oms_verify_success (const guint8 *rx, gsize size, gboolean saw_touch, guint16 slot)
{
  return saw_touch && oms_response_valid (rx, size) &&
         rx[7] == 0 && rx[8] == 8 && rx[9] == 0 && rx[10] == 5 &&
         (((guint16) rx[11] << 8) | rx[12]) == slot;
}

/* Vendor ReadIndexTable returns a status byte followed by 32 occupancy bytes.
 * This describes 256 addresses, not a guarantee of physical capacity. */
static inline gint
oms_first_free_slot (const guint8 *rx, gsize size)
{
  if (!oms_response_valid (rx, size) || rx[7] != 0 || rx[8] != 0x23 || rx[9] != 0)
    return -2;
  for (guint i = 0; i < 256; i++)
    if (!(rx[10 + i / 8] & (1u << (i % 8))))
      return i;
  return -1;
}

static inline gboolean
oms_print_slot (GVariant *data, guint16 *slot)
{
  g_autoptr(GVariant) uid = NULL;
  guint8 finger;
  gsize uid_len;

  if (!data)
    return FALSE;
  if (g_variant_is_of_type (data, G_VARIANT_TYPE ("(qyay)")))
    {
      g_variant_get (data, "(qy@ay)", slot, &finger, &uid);
      if (*slot >= 256 || finger < 1 || finger > 10)
        return FALSE;
    }
  else if (g_variant_is_of_type (data, G_VARIANT_TYPE ("(yay)")))
    {
      g_variant_get (data, "(y@ay)", &finger, &uid);
      if (finger != 0xff)
        return FALSE;
      *slot = 0; /* Pre-slot-mapping records always used slot zero. */
    }
  else
    return FALSE;
  g_variant_get_fixed_array (uid, &uid_len, 1);
  return uid_len > 0 && uid_len <= 16;
}

static inline gboolean
oms_delete_success (const guint8 *rx, gsize size)
{
  return oms_response_valid (rx, size) && rx[7] == 0 && rx[8] == 3 && rx[9] == 0;
}
