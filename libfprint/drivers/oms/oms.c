/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Copyright (C) 2026 libfprint-oms-driver contributors
 * Modified for public source distribution on 2026-10-25.
 */

/*
 * OMS / OFilm fingerprint driver for 33a7:2388 - asynchronous transport.
 * Protocol from OMSFpDriver.dll (OMS 1.0.0.29); see docs/protocol.md.
 */

#include "drivers_api.h"
#include "fpi-usb-transfer.h"
#include "oms-protocol.h"

#define OMS_VID 0x33a7
#define OMS_PID 0x2388
#define OMS_EP_OUT 0x01
#define OMS_EP_IN 0x81

#define OMS_PKT_LEN 64
#define OMS_TIMEOUT 2000

typedef enum
{
  OMS_OP_NONE,
  OMS_OP_OPEN,
  OMS_OP_ENROLL_CANCEL,
  OMS_OP_ENROLL_INDEX,
  OMS_OP_ENROLL,
  OMS_OP_ENROLL_POLL,
  OMS_OP_VERIFY_CANCEL,
  OMS_OP_VERIFY,
  OMS_OP_DELETE_CANCEL,
  OMS_OP_DELETE,
  OMS_OP_CLOSE_CANCEL,
  OMS_OP_CLOSE_SLEEP,
} OmsOp;

struct _FpiDeviceOms
{
  FpDevice parent;
  guint8 ep_in;
  guint8 ep_out;
  FpPrint *enroll_print;
  OmsOp op;
  gboolean has_cmd;
  guint8 hdr[8];
  guint8 tx[OMS_PKT_LEN];
  gsize tx_len;
  guint8 rx[OMS_PKT_LEN];
  guint8 drain[OMS_PKT_LEN];
  gint polls;
  gint retries;
  guint8 stages;
  gsize read_len;
  gsize rx_len;
  gboolean closed;
  gboolean saw_progress;
  gboolean identifying;
  gint64 deadline_us;
  guint16 slot;
};

G_DECLARE_FINAL_TYPE (FpiDeviceOms, fpi_device_oms, FPI, DEVICE_OMS, FpDevice)
G_DEFINE_TYPE (FpiDeviceOms, fpi_device_oms, FP_TYPE_DEVICE)

static const FpIdEntry id_table[] = {
  { .vid = OMS_VID, .pid = OMS_PID },
  { .vid = 0, .pid = 0 },
};

static gsize
oms_build_short (guint8 *out, const guint8 *body, gsize body_len)
{
  guint8 sum = 0;
  gsize i;

  out[0] = 0xef; out[1] = 0x01; out[2] = 0xff; out[3] = 0xff;
  out[4] = 0xff; out[5] = 0xff; out[6] = 0x01; out[7] = 0x00;
  memcpy (out + 8, body, body_len);
  for (i = 6; i < 8 + body_len; i++)
    sum += out[i];
  out[8 + body_len] = sum;
  return 9 + body_len;
}

static gsize
oms_build_long (guint8 *out, guint8 opcode, const guint8 params[5])
{
  guint16 sum = 0;
  gsize i;

  out[0] = 0xef; out[1] = 0x01; out[2] = 0xff; out[3] = 0xff;
  out[4] = 0xff; out[5] = 0xff; out[6] = 0x01; out[7] = 0x00;
  out[8] = 0x08;
  out[9] = opcode;
  memcpy (out + 10, params, 5);
  for (i = 6; i < 15; i++)
    sum += out[i];
  out[15] = sum >> 8;
  out[16] = sum & 0xff;
  return 17;
}

/* fprintd marshals the print over D-Bus; without a valid "fpi-data" variant it
 * trips a GVariant assertion and the daemon dumps core. */
static void
oms_prepare_print (FpDevice *device)
{
  FpPrint *print = NULL;
  g_autofree gchar *uid = NULL;
  g_autofree guint8 *payload = NULL;
  GVariant *data;
  gsize uid_len;

  fpi_device_get_enroll_data (device, &print);
  if (!print)
    return;

  uid = fpi_print_generate_user_id (print);
  uid_len = MIN (16, strlen (uid));
  payload = g_malloc0 (16);
  memcpy (payload, uid, uid_len);

  data = g_variant_new ("(qy@ay)", FPI_DEVICE_OMS (device)->slot,
                        fp_print_get_finger (print),
                        g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE,
                                                   payload, uid_len, 1));
  fpi_print_set_type (print, FPI_PRINT_RAW);
  fpi_print_set_device_stored (print, TRUE);
  g_object_set (print, "fpi-data", data, NULL);
  g_object_set (print, "description", uid, NULL);
}

static void
oms_xfer_cb (FpiUsbTransfer *transfer, FpDevice *dev, gpointer user_data,
             GError *error)
{
  FpiSsm *ssm = user_data;

  if (error)
    {
      fpi_ssm_mark_failed (ssm, error);
      return;
    }
  fpi_ssm_next_state (ssm);
}

static void
oms_submit_to (FpDevice *device, FpiSsm *ssm, guint8 ep, guint8 *buf, gsize len,
               FpiUsbTransferCallback cb, guint timeout)
{
  FpiUsbTransfer *transfer = fpi_usb_transfer_new (device);

  fpi_usb_transfer_fill_bulk_full (transfer, ep, buf, len, NULL);
  fpi_usb_transfer_set_short_error (transfer, FALSE);
  fpi_usb_transfer_submit (transfer, timeout, NULL, cb, ssm);
}

static void
oms_submit_cb (FpDevice *device, FpiSsm *ssm, guint8 ep, guint8 *buf, gsize len,
               FpiUsbTransferCallback cb)
{
  oms_submit_to (device, ssm, ep, buf, len, cb, OMS_TIMEOUT);
}

/* Drain any packet the chip still holds; never fails the operation. */
static void
oms_drain_cb (FpiUsbTransfer *transfer, FpDevice *dev, gpointer user_data,
              GError *error)
{
  FpiSsm *ssm = user_data;
  FpiDeviceOms *self = FPI_DEVICE_OMS (dev);

  /* The index table may follow an acknowledgment in the second USB read. */
  if (!error && self->op == OMS_OP_ENROLL_INDEX &&
      oms_first_free_slot (self->drain, transfer->actual_length) != -2)
    {
      memcpy (self->rx, self->drain, sizeof (self->rx));
      self->rx_len = transfer->actual_length;
    }

  if (!error && self->op == OMS_OP_VERIFY &&
      oms_response_valid (self->drain, transfer->actual_length) &&
      self->drain[8] == 8)
    {
      memcpy (self->rx, self->drain, sizeof self->rx);
      self->rx_len = transfer->actual_length;
      if (oms_verify_touch (self->rx, self->rx_len))
        self->saw_progress = TRUE;
    }
  g_clear_error (&error);
  fpi_ssm_next_state (ssm);
}

static void
oms_submit (FpDevice *device, FpiSsm *ssm, guint8 ep, guint8 *buf, gsize len)
{
  oms_submit_cb (device, ssm, ep, buf, len, oms_xfer_cb);
}

/* Data read: skip the transport ack ("USBS") and tolerate empty polls. */
static void
oms_read_cb (FpiUsbTransfer *transfer, FpDevice *dev, gpointer user_data,
             GError *error)
{
  FpiSsm *ssm = user_data;
  FpiDeviceOms *self = FPI_DEVICE_OMS (dev);

  if (error)
    {
      memset (self->rx, 0, sizeof (self->rx));
      self->rx_len = 0;
      if (self->op == OMS_OP_ENROLL_POLL || self->op == OMS_OP_VERIFY)
        {
          g_clear_error (&error);
          fpi_ssm_next_state (ssm);
          return;
        }
      fpi_ssm_mark_failed (ssm, error);
      return;
    }

  self->rx_len = transfer->actual_length;
  if (self->op == OMS_OP_VERIFY && oms_verify_touch (self->rx, self->rx_len))
    self->saw_progress = TRUE;
  if (memcmp (self->rx, "USBS", 4) == 0 && self->retries < 2)
    {
      self->retries++;
      oms_submit_cb (dev, ssm, OMS_EP_IN, self->rx, OMS_PKT_LEN, oms_read_cb);
      return;
    }

  fpi_ssm_next_state (ssm);
}

static void
oms_pkt_init (FpiDeviceOms *self)
{
  memset (self->tx, 0, sizeof (self->tx));
}

static void
oms_hdr_fill (FpiDeviceOms *self, guint8 direction, guint32 len)
{
  self->hdr[0] = 0x55; self->hdr[1] = 0x53;
  self->hdr[2] = 0x42; self->hdr[3] = 0x43;
  self->hdr[4] = direction;
  self->hdr[5] = len & 0xff;
  self->hdr[6] = (len >> 8) & 0xff;
  self->hdr[7] = (len >> 16) & 0xff;
}

static void oms_run_op (FpDevice *device);

static void
oms_txn_ssm (FpiSsm *ssm, FpDevice *device)
{
  FpiDeviceOms *self = FPI_DEVICE_OMS (device);

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case 0:
      if (!self->has_cmd)
        {
          fpi_ssm_jump_to_state (ssm, 3);
          return;
        }
      oms_hdr_fill (self, 0x00, OMS_PKT_LEN);
      oms_submit (device, ssm, OMS_EP_OUT, self->hdr, sizeof (self->hdr));
      break;
    case 1:
      oms_submit (device, ssm, OMS_EP_OUT, self->tx, OMS_PKT_LEN);
      break;
    case 2:
      oms_submit (device, ssm, OMS_EP_IN, self->rx, OMS_PKT_LEN);
      break;
    case 3:
      memset (self->rx, 0, sizeof (self->rx));
      self->rx_len = 0;
      oms_hdr_fill (self, 0x80, self->read_len);
      oms_submit (device, ssm, OMS_EP_OUT, self->hdr, sizeof (self->hdr));
      break;
    case 4:
      self->retries = 0;
      oms_submit_cb (device, ssm, OMS_EP_IN, self->rx, self->read_len, oms_read_cb);
      break;

    case 5:
      /* The vendor driver reads twice after every command; leaving the second
       * packet unread makes the chip stop answering subsequent commands. */
      memset (self->drain, 0, sizeof (self->drain));
      oms_submit_to (device, ssm, OMS_EP_IN, self->drain, OMS_PKT_LEN,
                     oms_drain_cb, 400);
      break;
    }
}

static guint16
oms_len (const guint8 *rx)
{
  return ((guint16) rx[7] << 8) | rx[8];
}

static void oms_close_done (FpDevice *device);

static void
oms_fail (FpDevice *device, GError *error)
{
  FpiDeviceOms *self = FPI_DEVICE_OMS (device);

  switch (self->op)
    {
    case OMS_OP_OPEN:
      fpi_device_open_complete (device, error);
      break;
    case OMS_OP_VERIFY_CANCEL:
    case OMS_OP_VERIFY:
      if (self->identifying)
        fpi_device_identify_complete (device, error);
      else
        fpi_device_verify_complete (device, error);
      break;
    case OMS_OP_DELETE_CANCEL:
    case OMS_OP_DELETE:
      fpi_device_delete_complete (device, error);
      break;
    case OMS_OP_CLOSE_CANCEL:
    case OMS_OP_CLOSE_SLEEP:
      g_clear_error (&error);
      oms_close_done (device);
      break;
    case OMS_OP_NONE:
    case OMS_OP_ENROLL_CANCEL:
    case OMS_OP_ENROLL_INDEX:
    case OMS_OP_ENROLL:
    case OMS_OP_ENROLL_POLL:
      fpi_device_enroll_complete (device, NULL, error);
      break;
    }
}

static void
oms_txn_done (FpiSsm *ssm, FpDevice *device, GError *error)
{
  FpiDeviceOms *self = FPI_DEVICE_OMS (device);

  fp_dbg ("oms: op=%d rx=%02x %02x %02x %02x %02x %02x %02x %02x | %02x %02x %02x %02x %02x %02x",
          self->op, self->rx[0], self->rx[1], self->rx[2], self->rx[3],
          self->rx[4], self->rx[5], self->rx[6], self->rx[7],
          self->rx[8], self->rx[9], self->rx[10], self->rx[11],
          self->rx[12], self->rx[13]);

  if (error)
    {
      oms_fail (device, error);
      return;
    }
  oms_run_op (device);
}

static void
oms_start_txn (FpDevice *device)
{
  FpiSsm *ssm = fpi_ssm_new (device, oms_txn_ssm, 6);

  fpi_ssm_start (ssm, oms_txn_done);
}

static gboolean
oms_delayed_start_cb (gpointer data)
{
  FpDevice *device = data;

  oms_start_txn (device);
  g_object_unref (device);
  return G_SOURCE_REMOVE;
}

/* The vendor driver sleeps 50 ms between commands; back-to-back transactions
 * make the chip stop answering after the first one. */
static void
oms_start_txn_delayed (FpDevice *device)
{
  g_timeout_add (60, oms_delayed_start_cb, g_object_ref (device));
}

/* Search only the gallery supplied by fprintd for this authentication.
 * A slot belonging to another user's print must never count as a match. */
static void
oms_report_scan (FpDevice *device, gboolean timed_out)
{
  FpiDeviceOms *self = FPI_DEVICE_OMS (device);

  if (self->identifying)
    {
      GPtrArray *prints = NULL;
      FpPrint *match = NULL;

      fpi_device_get_identify_data (device, &prints);
      for (guint i = 0; !timed_out && prints && i < prints->len; i++)
        {
          FpPrint *print = g_ptr_array_index (prints, i);
          g_autoptr(GVariant) data = NULL;
          guint16 slot;

          g_object_get (print, "fpi-data", &data, NULL);
          if (oms_print_slot (data, &slot) &&
              oms_verify_success (self->rx, self->rx_len, self->saw_progress, slot))
            {
              match = print;
              break;
            }
        }
      fp_dbg ("oms: identify -> %s", match ? "match in requested gallery" : "no match");
      fpi_device_identify_report (device, match, NULL, NULL);
      fpi_device_identify_complete (device, NULL);
    }
  else
    {
      gboolean matched = !timed_out &&
        oms_verify_success (self->rx, self->rx_len, self->saw_progress, self->slot);

      fp_dbg ("oms: verify requested slot %u -> %s", self->slot,
              matched ? "match" : "no match");
      fpi_device_verify_report (device, matched ? FPI_MATCH_SUCCESS : FPI_MATCH_FAIL,
                                NULL, NULL);
      fpi_device_verify_complete (device, NULL);
    }
}

static void
oms_run_op (FpDevice *device)
{
  FpiDeviceOms *self = FPI_DEVICE_OMS (device);

  if ((self->op == OMS_OP_VERIFY_CANCEL || self->op == OMS_OP_VERIFY || self->op == OMS_OP_ENROLL_CANCEL ||
       self->op == OMS_OP_ENROLL_INDEX || self->op == OMS_OP_ENROLL ||
       self->op == OMS_OP_ENROLL_POLL) &&
      fpi_device_action_is_cancelled (device))
    {
      oms_fail (device, g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                             "Fingerprint operation cancelled"));
      return;
    }

  switch (self->op)
    {
    case OMS_OP_OPEN:
      {
        gboolean all_zero = TRUE;
        for (guint i = 0; i < OMS_PKT_LEN; i++)
          if (self->rx[i] != 0)
            all_zero = FALSE;

        /* The chip answers the handshake only once per power cycle; an
         * all-zero reply means the session is already open. */
        if (!oms_response_valid (self->rx, self->rx_len) && !all_zero)
          {
            fpi_device_open_complete (device,
                                      fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                                "handshake rejected"));
            return;
          }
      }
      fpi_device_open_complete (device, NULL);
      return;

    case OMS_OP_VERIFY_CANCEL:
      /* Discard cancel/latched replies before establishing a new touch gate. */
      self->op = OMS_OP_VERIFY;
      self->saw_progress = FALSE;
      self->deadline_us = g_get_monotonic_time () + 12 * G_USEC_PER_SEC;
      self->has_cmd = TRUE;
      self->read_len = OMS_PKT_LEN;
      oms_pkt_init (self);
      self->tx_len = oms_build_long (self->tx, 0x32,
                                     (const guint8[]){5, 0xff, 0xff, 0, 0});
      oms_start_txn_delayed (device);
      return;

    case OMS_OP_ENROLL_CANCEL:
      self->op = OMS_OP_ENROLL_INDEX;
      self->polls = 0;
      self->has_cmd = TRUE;
      self->read_len = OMS_PKT_LEN;
      oms_pkt_init (self);
      self->tx_len = oms_build_short (self->tx,
                                       (const guint8[]){0x04, 0x1f, 0, 0}, 4);
      oms_start_txn_delayed (device);
      return;

    case OMS_OP_ENROLL_INDEX:
      {
        gint slot = oms_first_free_slot (self->rx, self->rx_len);

        /* A delayed command acknowledgment can precede the index table. */
        if (slot == -2 && ++self->polls < 5 &&
            (self->rx_len == 0 || self->rx[0] == 0 ||
             oms_delete_success (self->rx, self->rx_len)))
          {
            self->has_cmd = FALSE;
            oms_start_txn_delayed (device);
            return;
          }

        if (slot < 0)
          {
            oms_fail (device,
                      fpi_device_error_new_msg (slot == -1 ? FP_DEVICE_ERROR_DATA_FULL :
                                                FP_DEVICE_ERROR_PROTO,
                                                "Cannot allocate sensor slot: %s",
                                                slot == -1 ? "index table full" : "invalid index response"));
            return;
          }
        self->slot = slot;
        fp_dbg ("oms: allocating free sensor slot %u", self->slot);
        self->op = OMS_OP_ENROLL;
        self->has_cmd = TRUE;
        self->read_len = 0x20;
        oms_pkt_init (self);
        self->tx_len = oms_build_long (self->tx, 0x31,
                                       (const guint8[]){self->slot >> 8,
                                                       self->slot & 0xff, 6, 0, 0});
        oms_start_txn_delayed (device);
        return;
      }

    case OMS_OP_DELETE_CANCEL:
      self->op = OMS_OP_DELETE;
      self->has_cmd = TRUE;
      self->read_len = OMS_PKT_LEN;
      oms_pkt_init (self);
      self->tx_len = oms_build_delete (self->tx, self->slot);
      oms_start_txn_delayed (device);
      return;

    case OMS_OP_DELETE:
      if (oms_delete_success (self->rx, self->rx_len))
        {
          fpi_device_delete_complete (device, NULL);
          return;
        }
      /* An empty transport read is not a successful delete. Read again
       * without resending the destructive command. */
      if (oms_response_valid (self->rx, self->rx_len) ||
          g_get_monotonic_time () >= self->deadline_us)
        {
          oms_fail (device,
                    fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                              "Sensor did not confirm template deletion"));
          return;
        }
      self->has_cmd = FALSE;
      oms_start_txn_delayed (device);
      return;

    case OMS_OP_ENROLL:
      self->op = OMS_OP_ENROLL_POLL;
      self->has_cmd = FALSE;
      self->read_len = 0x20;
      self->polls = 0;
      self->stages = 0;
      /* The first read can already contain a rejection or progress. */
      oms_run_op (device);
      return;

    case OMS_OP_CLOSE_CANCEL:
      self->op = OMS_OP_CLOSE_SLEEP;
      self->has_cmd = TRUE;
      self->read_len = OMS_PKT_LEN;
      oms_pkt_init (self);
      self->tx_len = oms_build_short (self->tx, (const guint8[]){ 0x03, 0x33, 0x00 }, 3);
      oms_start_txn_delayed (device);
      return;

    case OMS_OP_CLOSE_SLEEP:
      oms_close_done (device);
      return;

    case OMS_OP_VERIFY:
    case OMS_OP_ENROLL_POLL:
      {
        /* Send the command once, then only read progress frames. Reissuing
         * identify on every poll resets the scan and can replay old results. */
        self->has_cmd = FALSE;
        /* Verify result: LEN=8 frame, status byte 0x05, id in bytes 11..12.
         * 0xffff means no match (OMSAutoIdentify "ret != 0" path). */
        /* Remember a real finger event: a 0x05 result is only trustworthy
         * after the chip has reported a capture, otherwise a stale/latched
         * answer would unlock without any finger present. */
        if (self->op == OMS_OP_VERIFY && oms_verify_touch (self->rx, self->rx_len))
          self->saw_progress = TRUE;

        if (self->op == OMS_OP_VERIFY && oms_response_valid (self->rx, self->rx_len) &&
            self->rx[8] == 8 && self->rx[10] == 5 && self->saw_progress)
          {
            oms_report_scan (device, FALSE);
            return;
          }

        /* Enroll frames: EF 01 ... 05 <code> <status> <count> */
        if (self->op == OMS_OP_ENROLL_POLL &&
            oms_response_valid (self->rx, self->rx_len) && self->rx[8] == 0x05)
          {
            guint8 code = self->rx[9];
            guint8 status = self->rx[10];

            fp_dbg ("oms: poll %d len=%u status=0x%02x code=0x%02x count=%u",
                    self->polls, oms_len (self->rx), status, code, self->rx[11]);

            switch (status)
              {
              case 0x03: /* finger event; count is the chip's capture number */
                {
                  guint8 count = self->rx[11];

                  if (count > 5)
                    count = 5; /* must stay below nr_enroll_stages */
                  if (count != self->stages)
                    {
                      self->stages = count;
                      fp_dbg ("oms: enroll stage %u/6 (count=%u)",
                              self->stages, self->rx[11]);
                      fpi_device_enroll_progress (device, self->stages, NULL, NULL);
                    }
                }
                break;
              case 0x00: /* chip reported an error before finishing */
                if (code == 0x22 && self->op == OMS_OP_ENROLL_POLL)
                  {
                    /* The existing template's owner is unknown. Never
                     * silently bind it to a new user or a different finger. */
                    oms_fail (device,
                              fpi_device_error_new_msg (FP_DEVICE_ERROR_DATA_FULL,
                                                        "Allocated sensor slot is occupied; retry enrollment"));
                    return;
                  }
                break;

              case 0x06: /* finished */
                if (code == 0)
                  {
                    if (self->op == OMS_OP_ENROLL_POLL)
                      {
                        FpPrint *print = NULL;

                        oms_prepare_print (device);
                        fpi_device_get_enroll_data (device, &print);
                        if (print)
                          fpi_device_enroll_complete (device, g_object_ref (print), NULL);
                      }
                    return;
                  }
                oms_fail (device, fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                                            "sensor reported error"));
                return;
              case 0x01:
              case 0x02:
              case 0x04:
              case 0x05:
                /* error codes are reported again in the status 6 frame */
                break;
              default:
                break;
              }
          }

        self->polls++;
        if (g_get_monotonic_time () >= self->deadline_us)
          {
            if (self->op == OMS_OP_VERIFY)
              {
                /* No finger within the window: a clean "no match" lets PAM
                 * fall back to the password prompt. */
                oms_report_scan (device, TRUE);
                return;
              }
            oms_fail (device, fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                                        "timeout waiting for finger"));
            return;
          }
        oms_start_txn_delayed (device);
        return;
      }

    case OMS_OP_NONE:
      return;
    }
}

static void
oms_open (FpDevice *device)
{
  FpiDeviceOms *self = FPI_DEVICE_OMS (device);
  GUsbDevice *usb = fpi_device_get_usb_device (device);
  GError *error = NULL;

  if (!g_usb_device_claim_interface (usb, 0,
                                     G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
                                     &error))
    {
      fpi_device_open_complete (device, error);
      return;
    }

  self->op = OMS_OP_OPEN;
  self->has_cmd = TRUE;
  self->read_len = OMS_PKT_LEN;
  oms_pkt_init (self);
  self->tx_len = oms_build_short (self->tx, (const guint8[]){ 0x03, 0x35, 0x00 }, 3) ;
  oms_start_txn (device);
}

static void
oms_close_done (FpDevice *device)
{
  FpiDeviceOms *self = FPI_DEVICE_OMS (device);

  if (self->closed)
    return;
  self->closed = TRUE;
  g_usb_device_release_interface (fpi_device_get_usb_device (device), 0,
                                  G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
                                  NULL);
  fpi_device_close_complete (device, NULL);
}

static void
oms_close (FpDevice *device)
{
  FpiDeviceOms *self = FPI_DEVICE_OMS (device);

  /* Leave the chip in a clean state: cancel any pending operation and put it
   * back to sleep, so the next open does not inherit a half-finished scan. */
  self->closed = FALSE;
  self->op = OMS_OP_CLOSE_CANCEL;
  self->has_cmd = TRUE;
  self->read_len = OMS_PKT_LEN;
  oms_pkt_init (self);
  self->tx_len = oms_build_short (self->tx, (const guint8[]){ 0x03, 0x30, 0x00 }, 3);
  oms_start_txn_delayed (device);
}

static void
oms_enroll (FpDevice *device)
{
  FpiDeviceOms *self = FPI_DEVICE_OMS (device);
  FpPrint *print = NULL;

  fpi_device_get_enroll_data (device, &print);
  if (print)
    {
      fpi_print_set_type (print, FPI_PRINT_RAW);
      fpi_print_set_device_stored (print, TRUE);
    }

  self->op = OMS_OP_ENROLL_CANCEL;
  self->deadline_us = g_get_monotonic_time () + 120 * G_USEC_PER_SEC;
  self->has_cmd = TRUE;
  self->read_len = OMS_PKT_LEN;
  oms_pkt_init (self);
  self->tx_len = oms_build_short (self->tx, (const guint8[]){ 0x03, 0x30, 0x00 }, 3) ;
  oms_start_txn (device);
}

static void
oms_scan_start (FpDevice *device)
{
  FpiDeviceOms *self = FPI_DEVICE_OMS (device);

  self->op = OMS_OP_VERIFY_CANCEL;
  self->saw_progress = FALSE;
  self->has_cmd = TRUE;
  self->read_len = OMS_PKT_LEN;
  oms_pkt_init (self);
  self->tx_len = oms_build_short (self->tx, (const guint8[]){3, 0x30, 0}, 3);
  oms_start_txn (device);
}

static void
oms_identify (FpDevice *device)
{
  FpiDeviceOms *self = FPI_DEVICE_OMS (device);

  self->identifying = TRUE;
  oms_scan_start (device);
}

static void
oms_verify (FpDevice *device)
{
  FpiDeviceOms *self = FPI_DEVICE_OMS (device);

  FpPrint *print = NULL;
  g_autoptr(GVariant) data = NULL;

  fpi_device_get_verify_data (device, &print);
  if (print)
    g_object_get (print, "fpi-data", &data, NULL);
  if (!oms_print_slot (data, &self->slot))
    {
      fpi_device_verify_complete (device,
                                  fpi_device_error_new (FP_DEVICE_ERROR_DATA_INVALID));
      return;
    }

  self->identifying = FALSE;
  oms_scan_start (device);
}

static void
oms_delete (FpDevice *device)
{
  FpiDeviceOms *self = FPI_DEVICE_OMS (device);
  FpPrint *print = NULL;
  g_autoptr(GVariant) data = NULL;
  /* Use the slot saved by enrollment; reject unknown record formats. */
  fpi_device_get_delete_data (device, &print);
  if (print)
    g_object_get (print, "fpi-data", &data, NULL);
  if (!oms_print_slot (data, &self->slot))
    goto invalid;

  self->op = OMS_OP_DELETE_CANCEL;
  self->deadline_us = g_get_monotonic_time () + 8 * G_USEC_PER_SEC;
  self->has_cmd = TRUE;
  self->read_len = OMS_PKT_LEN;
  oms_pkt_init (self);
  self->tx_len = oms_build_short (self->tx, (const guint8[]){0x03, 0x30, 0}, 3);
  oms_start_txn (device);
  return;

invalid:
  fpi_device_delete_complete (device,
                              fpi_device_error_new (FP_DEVICE_ERROR_DATA_INVALID));
}

static void
fpi_device_oms_init (FpiDeviceOms *self)
{
  self->ep_in = OMS_EP_IN;
  self->ep_out = OMS_EP_OUT;
}

static void
fpi_device_oms_class_init (FpiDeviceOmsClass *klass)
{
  FpDeviceClass *dev_class = FP_DEVICE_CLASS (klass);

  dev_class->id = "oms";
  dev_class->full_name = "OMS/OFilm MOC Fingerprint Sensor";
  dev_class->type = FP_DEVICE_TYPE_USB;
  dev_class->scan_type = FP_SCAN_TYPE_PRESS;
  dev_class->id_table = id_table;
  dev_class->nr_enroll_stages = 6;

  dev_class->open = oms_open;
  dev_class->close = oms_close;
  dev_class->enroll = oms_enroll;
  dev_class->verify = oms_verify;
  dev_class->identify = oms_identify;
  dev_class->delete = oms_delete;

  fpi_device_class_auto_initialize_features (dev_class);
}
