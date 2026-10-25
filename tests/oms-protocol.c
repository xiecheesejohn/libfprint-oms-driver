/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Copyright (C) 2026 libfprint-oms-driver contributors
 * Modified for public source distribution on 2026-10-25.
 */

#include "../libfprint/drivers/oms/oms-protocol.h"

static void
test_verification (void)
{
  guint8 match[] = {0xef, 1, 0xff, 0xff, 0xff, 0xff, 7, 0, 8,
                    0, 5, 0, 0, 0, 0, 0, 0x14};
  guint8 no_match[] = {0xef, 1, 0xff, 0xff, 0xff, 0xff, 7, 0, 8,
                       9, 5, 0xff, 0xff, 0, 0, 2, 0x1b};

  g_assert_true (oms_response_valid (match, sizeof match));
  g_assert_true (oms_verify_success (match, sizeof match, TRUE, 0));
  g_assert_false (oms_verify_success (match, sizeof match, FALSE, 0));
  g_assert_true (oms_response_valid (no_match, sizeof no_match));
  g_assert_false (oms_verify_success (no_match, sizeof no_match, TRUE, 0));
  /* A valid result for another chip slot cannot verify the requested slot. */
  match[12] = 1;
  match[16]++;
  g_assert_true (oms_response_valid (match, sizeof match));
  g_assert_false (oms_verify_success (match, sizeof match, TRUE, 0));
  g_assert_true (oms_verify_success (match, sizeof match, TRUE, 1));
  /* Corruption and short USB transfers must never authenticate. */
  match[12] = 0;
  g_assert_false (oms_response_valid (match, sizeof match));
  match[16]--;
  for (gsize n = 0; n < sizeof match; n++)
    g_assert_false (oms_verify_success (match, n, TRUE, 0));
}

static void
test_live_touch_sequence (void)
{
  /* 04:40:12 touch then 04:40:13 result: the sensor uses status 1, not 3. */
  guint8 touch[] = {0xef, 1, 0xff, 0xff, 0xff, 0xff, 7, 0, 8,
                    0, 1, 0, 0, 0, 0, 0, 0x10};
  guint8 result[] = {0xef, 1, 0xff, 0xff, 0xff, 0xff, 7, 0, 8,
                     0, 5, 0, 1, 3, 0, 0, 0x18};
  gboolean saw_touch = FALSE;

  g_assert_false (oms_verify_success (result, sizeof result, saw_touch, 1));
  g_assert_true (oms_verify_touch (touch, sizeof touch));
  saw_touch = oms_verify_touch (touch, sizeof touch);
  g_assert_true (oms_verify_success (result, sizeof result, saw_touch, 1));
  g_assert_false (oms_verify_success (result, sizeof result, saw_touch, 0));
  g_assert_false (oms_verify_touch (result, sizeof result));
  touch[10] = 0;
  touch[16]--;
  g_assert_true (oms_response_valid (touch, sizeof touch));
  g_assert_false (oms_verify_touch (touch, sizeof touch));
  touch[10] = 3;
  touch[16] += 3;
  g_assert_true (oms_response_valid (touch, sizeof touch));
  g_assert_false (oms_verify_touch (touch, sizeof touch));
}

static void
test_slots (void)
{
  guint8 index[44] = {0xef, 1, 0xff, 0xff, 0xff, 0xff, 7, 0, 0x23};
  guint16 sum = 7 + 0x23;
  guint16 slot;
  const guint8 user[] = {'t', 'e', 's', 't'};
  g_autoptr(GVariant) legacy = g_variant_ref_sink (
    g_variant_new ("(y@ay)", 0xff,
                   g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, user, sizeof user, 1)));
  g_autoptr(GVariant) second = g_variant_ref_sink (
    g_variant_new ("(qy@ay)", 1, 8,
                   g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, user, sizeof user, 1)));
  g_autoptr(GVariant) invalid = g_variant_ref_sink (g_variant_new_string ("invalid"));

  index[43] = sum;
  g_assert_cmpint (oms_first_free_slot (index, sizeof index), ==, 0);
  index[10] = 1;
  index[43]++;
  g_assert_cmpint (oms_first_free_slot (index, sizeof index), ==, 1);
  index[10] = 0xff;
  index[42] = (sum + 0xff) >> 8;
  index[43] = (sum + 0xff) & 0xff;
  g_assert_cmpint (oms_first_free_slot (index, sizeof index), ==, 8);
  memset (index + 10, 0xff, 32);
  sum += 32 * 0xff;
  index[42] = sum >> 8;
  index[43] = sum & 0xff;
  g_assert_cmpint (oms_first_free_slot (index, sizeof index), ==, -1);
  g_assert_cmpint (oms_first_free_slot (index, sizeof index - 1), ==, -2);
  index[43]++;
  g_assert_cmpint (oms_first_free_slot (index, sizeof index), ==, -2);
  g_assert_true (oms_print_slot (legacy, &slot));
  g_assert_cmpuint (slot, ==, 0);
  g_assert_true (oms_print_slot (second, &slot));
  g_assert_cmpuint (slot, ==, 1);
  g_assert_false (oms_print_slot (invalid, &slot));
  g_assert_false (oms_print_slot (NULL, &slot));
}

static void
test_deletion (void)
{
  guint8 command[16] = {0};
  const guint8 expected[] = {0xef, 1, 0xff, 0xff, 0xff, 0xff, 1, 0,
                             7, 0x0c, 0, 0, 0, 1, 0, 0x15};
  guint8 ack[] = {0xef, 1, 0xff, 0xff, 0xff, 0xff, 7, 0, 3, 0, 0, 0x0a};
  guint8 empty[64] = {0};

  g_assert_cmpuint (oms_build_delete (command, 0), ==, sizeof expected);
  g_assert_cmpmem (command, sizeof command, expected, sizeof expected);
  g_assert_true (oms_delete_success (ack, sizeof ack));
  g_assert_false (oms_delete_success (empty, sizeof empty));
  g_assert_false (oms_delete_success (ack, sizeof ack - 1));
  ack[9] = 1;
  ack[11]++;
  g_assert_true (oms_response_valid (ack, sizeof ack));
  g_assert_false (oms_delete_success (ack, sizeof ack));
}

static void
test_enrollment_capture (void)
{
  /* Actual progress and completion frames from local_capture_package.pcapng. */
  guint8 progress[] = {0xef, 1, 0xff, 0xff, 0xff, 0xff, 7, 0, 5,
                       0, 3, 1, 0, 0x10};
  guint8 complete[] = {0xef, 1, 0xff, 0xff, 0xff, 0xff, 7, 0, 5,
                       0, 6, 0xf2, 1, 4};

  g_assert_true (oms_response_valid (progress, sizeof progress));
  g_assert_true (oms_response_valid (complete, sizeof complete));
  g_assert_false (oms_verify_success (complete, sizeof complete, TRUE, 0));
  complete[7] = 0xff;
  g_assert_false (oms_response_valid (complete, sizeof complete));
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/oms/verification", test_verification);
  g_test_add_func ("/oms/deletion", test_deletion);
  g_test_add_func ("/oms/slots", test_slots);
  g_test_add_func ("/oms/live-touch-sequence", test_live_touch_sequence);
  g_test_add_func ("/oms/enrollment-capture", test_enrollment_capture);
  return g_test_run ();
}
