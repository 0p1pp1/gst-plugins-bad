#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gst/gst.h"

#include <rfb.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include "vncauth.h"

#define RFB_GET_UINT32(ptr) GUINT32_FROM_BE (*(guint32 *)(ptr))
#define RFB_GET_UINT16(ptr) GUINT16_FROM_BE (*(guint16 *)(ptr))
#define RFB_GET_UINT8(ptr) (*(guint8 *)(ptr))

#define RFB_SET_UINT32(ptr, val) (*(guint32 *)(ptr) = GUINT32_TO_BE (val))
#define RFB_SET_UINT16(ptr, val) (*(guint16 *)(ptr) = GUINT16_TO_BE (val))
#define RFB_SET_UINT8(ptr, val) (*(guint8 *)(ptr) = val)

GST_DEBUG_CATEGORY_STATIC (rfbdecoder_debug);
#define GST_CAT_DEFAULT rfbdecoder_debug

#if 0
struct _RfbSocketPrivate
{
  gint fd;
  sockaddr sa;
}
#endif

static gboolean rfb_decoder_state_wait_for_protocol_version (RfbDecoder *
    decoder);
static gboolean rfb_decoder_state_wait_for_security (RfbDecoder * decoder);
static gboolean rfb_decoder_state_send_client_initialisation (RfbDecoder *
    decoder);
static gboolean rfb_decoder_state_wait_for_server_initialisation (RfbDecoder *
    decoder);
static gboolean rfb_decoder_state_security_result (RfbDecoder * decoder);
static gboolean rfb_decoder_state_normal (RfbDecoder * decoder);
static gboolean rfb_decoder_state_framebuffer_update (RfbDecoder * decoder);
static gboolean rfb_decoder_state_framebuffer_update_rectangle (RfbDecoder *
    decoder);
static gboolean rfb_decoder_state_set_colour_map_entries (RfbDecoder * decoder);
static gboolean rfb_decoder_state_server_cut_text (RfbDecoder * decoder);

RfbDecoder *
rfb_decoder_new (void)
{
  RfbDecoder *decoder = g_new0 (RfbDecoder, 1);

  decoder->fd = -1;

  decoder->password = NULL;

  decoder->offset_x = 0;
  decoder->offset_y = 0;
  decoder->rect_width = 0;
  decoder->rect_height = 0;

  return decoder;
}

void
rfb_decoder_free (RfbDecoder * decoder)
{
  g_return_if_fail (decoder != NULL);

  if (decoder->fd >= 0)
    close (decoder->fd);
}

gboolean
rfb_decoder_connect_tcp (RfbDecoder * decoder, gchar * addr, guint port)
{
  struct sockaddr_in sa;

  GST_DEBUG ("connecting to the rfb server");

  g_return_val_if_fail (decoder != NULL, FALSE);
  g_return_val_if_fail (decoder->fd == -1, FALSE);
  g_return_val_if_fail (addr != NULL, FALSE);

  decoder->fd = socket (PF_INET, SOCK_STREAM, 0);
  if (decoder->fd == -1) {
    GST_WARNING ("creating socket failed");
    return FALSE;
  }

  sa.sin_family = AF_INET;
  sa.sin_port = htons (port);
  inet_pton (AF_INET, addr, &sa.sin_addr);
  if (connect (decoder->fd, (struct sockaddr *) &sa,
          sizeof (struct sockaddr)) == -1) {
    close (decoder->fd);
    GST_WARNING ("connection failed");
    return FALSE;
  }
  //rfb_decoder_use_file_descriptor (decoder, fd);
  return TRUE;
}

/**
 * rfb_decoder_iterate:
 * @decoder: The rfb context
 *
 * Initializes the connection with the rfb server
 *
 * Returns: TRUE if initialization was succesfull, FALSE on fail.
 */
gboolean
rfb_decoder_iterate (RfbDecoder * decoder)
{
  GST_DEBUG_CATEGORY_INIT (rfbdecoder_debug, "rfbdecoder", 0, "Rfb source");

  g_return_val_if_fail (decoder != NULL, FALSE);
  g_return_val_if_fail (decoder->fd != -1, FALSE);

  if (decoder->state == NULL) {
    GST_DEBUG ("First iteration: set state to -> wait for protocol version");
    decoder->state = rfb_decoder_state_wait_for_protocol_version;
  }

  GST_DEBUG ("Executing next state in initialization");
  return decoder->state (decoder);
}

guint8 *
rfb_decoder_read (RfbDecoder * decoder, guint32 len)
{
  guint32 total = 0;
  guint32 now = 0;
  guint8 *address = NULL;

  g_return_val_if_fail (decoder->fd > 0, NULL);
  g_return_val_if_fail (len > 0, NULL);

  address = g_malloc (len);
  g_return_val_if_fail (address, NULL);

  while (total < len) {
    now = recv (decoder->fd, address + total, len - total, 0);
    if (now <= 0) {
      GST_WARNING ("rfb read error on socket");
      return NULL;
    }
    total += now;
  }
  return address;
}

static gint
rfb_decoder_send (RfbDecoder * decoder, guint8 * buffer, guint len)
{
  g_return_val_if_fail (decoder->fd != 0, FALSE);
  g_return_val_if_fail (buffer != NULL, FALSE);
  g_return_val_if_fail (len > 0, FALSE);

  return write (decoder->fd, buffer, len);
}

void
rfb_decoder_send_update_request (RfbDecoder * decoder,
    gboolean incremental, gint x, gint y, gint width, gint height)
{
  guint8 data[10];

  g_return_if_fail (decoder != NULL);
  g_return_if_fail (decoder->fd != -1);

  data[0] = 3;
  data[1] = incremental;
  RFB_SET_UINT16 (data + 2, x);
  RFB_SET_UINT16 (data + 4, y);
  RFB_SET_UINT16 (data + 6, width);
  RFB_SET_UINT16 (data + 8, height);

  rfb_decoder_send (decoder, data, 10);
}

void
rfb_decoder_send_key_event (RfbDecoder * decoder, guint key, gboolean down_flag)
{
  guint8 data[8];

  g_return_if_fail (decoder != NULL);
  g_return_if_fail (decoder->fd != -1);

  data[0] = 4;
  data[1] = down_flag;
  RFB_SET_UINT16 (data + 2, 0);
  RFB_SET_UINT32 (data + 4, key);

  rfb_decoder_send (decoder, data, 8);
}

void
rfb_decoder_send_pointer_event (RfbDecoder * decoder,
    gint button_mask, gint x, gint y)
{
  guint8 data[6];

  g_return_if_fail (decoder != NULL);
  g_return_if_fail (decoder->fd != -1);

  data[0] = 5;
  data[1] = button_mask;
  RFB_SET_UINT16 (data + 2, x);
  RFB_SET_UINT16 (data + 4, y);

  rfb_decoder_send (decoder, data, 6);
}

/**
 * rfb_decoder_state_wait_for_protocol_version:
 *
 * Negotiate the rfb version used
 *
 * \TODO Support for versions 3.7 and 3.8
 */
static gboolean
rfb_decoder_state_wait_for_protocol_version (RfbDecoder * decoder)
{
  guint8 *buffer = NULL;

  buffer = rfb_decoder_read (decoder, 12);

  g_return_val_if_fail (memcmp (buffer, "RFB 003.00", 10) == 0, FALSE);
  g_return_val_if_fail (*(buffer + 11) == 0x0a, FALSE);

  GST_DEBUG ("\"%.11s\"", buffer);
  *(buffer + 7) = 0x00;
  *(buffer + 11) = 0x00;
  decoder->protocol_major = atoi ((char *) (buffer + 4));
  decoder->protocol_minor = atoi ((char *) (buffer + 8));
  GST_DEBUG ("Major version : %d", decoder->protocol_major);
  GST_DEBUG ("Minor version : %d", decoder->protocol_minor);

  if (decoder->protocol_major != 3) {
    GST_INFO
        ("A major protocol version of %d is not supported, falling back to 3",
        decoder->protocol_major);
    decoder->protocol_major = 3;
  }
  switch (decoder->protocol_minor) {
    case 3:
      break;
    default:
      GST_INFO ("Minor version %d is not supported, using 3",
          decoder->protocol_minor);
      decoder->protocol_minor = 3;
  }
  rfb_decoder_send (decoder, (guint8 *) "RFB 003.003\n", 12);

  decoder->state = rfb_decoder_state_wait_for_security;
  g_free (buffer);
  return TRUE;
}

/**
 * a string describing the reason (where a string is specified as a length followed
 * by that many ASCII characters)
 **/
static gboolean
rfb_decoder_state_reason (RfbDecoder * decoder)
{
  gint reason_length;
  guint8 *buffer = NULL;

  buffer = rfb_decoder_read (decoder, 4);

  reason_length = RFB_GET_UINT32 (buffer);
  g_free (buffer);
  buffer = NULL;
  buffer = rfb_decoder_read (decoder, reason_length);
  GST_WARNING ("Reason by server: %s", buffer);

  g_free (buffer);

  return FALSE;
}

static gboolean
rfb_decoder_state_wait_for_security (RfbDecoder * decoder)
{
  guint8 *buffer = NULL;

  /**
   * Version 3.3 The server decides the security type and sends a single word
   *
   * The security-type may only take the value 0, 1 or 2. A value of 0 means that the
   * connection has failed and is followed by a string giving the reason, as described
   * above.
   */
  if (IS_VERSION_3_3 (decoder)) {
    buffer = rfb_decoder_read (decoder, 4);

    decoder->security_type = RFB_GET_UINT32 (buffer);
    GST_DEBUG ("security = %d", decoder->security_type);

    g_return_val_if_fail (decoder->security_type < 3, FALSE);
    g_return_val_if_fail (decoder->security_type != SECURITY_FAIL,
        rfb_decoder_state_reason (decoder));
    g_free (buffer);
    buffer = NULL;
  } else {
    /* \TODO Add behavoir for the rfb 3.7 and 3.8 servers */
    GST_WARNING ("Other versions are not yet supported");
  }

  switch (decoder->security_type) {
    case SECURITY_NONE:
      GST_DEBUG ("Security type is None");
      if (IS_VERSION_3_8 (decoder)) {
        decoder->state = rfb_decoder_state_security_result;
      } else {
        decoder->state = rfb_decoder_state_send_client_initialisation;
      }
      break;
    case SECURITY_VNC:
        /**
         * VNC authentication is to be used and protocol data is to be sent unencrypted. The
         * server sends a random 16-byte challenge
         */
      GST_DEBUG ("Security type is VNC Authentication");
      /* VNC Authentication can't be used if the password is not set */
      if (!decoder->password) {
        GST_WARNING
            ("VNC Authentication can't be used if the password is not set");
        return FALSE;
      }

      buffer = rfb_decoder_read (decoder, 16);
      vncEncryptBytes ((unsigned char *) buffer, decoder->password);
      rfb_decoder_send (decoder, buffer, 16);
      g_free (buffer);

      GST_DEBUG ("Encrypted challenge send to server");

      decoder->state = rfb_decoder_state_security_result;
      break;
    default:
      GST_WARNING ("Security type is not known");
      return FALSE;
      break;
  }
  return TRUE;
}

/**
 * The server sends a word to inform the client whether the security handshaking was
 * successful.
 */
static gboolean
rfb_decoder_state_security_result (RfbDecoder * decoder)
{
  guint8 *buffer = NULL;

  buffer = rfb_decoder_read (decoder, 4);
  if (RFB_GET_UINT32 (buffer) != 0) {
    GST_WARNING ("Security handshaking failed");
    if (IS_VERSION_3_8 (decoder)) {
      decoder->state = rfb_decoder_state_reason;
      return TRUE;
    }
    return FALSE;
  }

  GST_DEBUG ("Security handshaking succesfull");
  decoder->state = rfb_decoder_state_send_client_initialisation;

  return TRUE;
}

/**
 * rfb_decoder_state_set_encodings:
 * @decoder: The rfb context
 *
 * Sends the encoding types that the client can decode to the server
 *
 * Returns: TRUE if initialization was succesfull, FALSE on fail.
 */
static gboolean
rfb_decoder_state_set_encodings (RfbDecoder * decoder)
{
  guint8 *buffer = g_malloc0 (8);       // 4 + 4 * nr_of_encodings

  buffer[0] = 2;                // message-type
  buffer[3] = 1;                //  number of encodings

  /* RAW encoding (0) */

  rfb_decoder_send (decoder, buffer, 8);

  g_free (buffer);

  decoder->state = rfb_decoder_state_normal;

  return TRUE;
}

static gboolean
rfb_decoder_state_send_client_initialisation (RfbDecoder * decoder)
{
  guint8 shared_flag;

  shared_flag = decoder->shared_flag;
  rfb_decoder_send (decoder, &shared_flag, 1);
  GST_DEBUG ("shared_flag is %d", shared_flag);

  decoder->state = rfb_decoder_state_wait_for_server_initialisation;
  return TRUE;
}

static gboolean
rfb_decoder_state_wait_for_server_initialisation (RfbDecoder * decoder)
{
  guint8 *buffer = NULL;
  guint32 name_length;

  buffer = rfb_decoder_read (decoder, 24);

  decoder->width = RFB_GET_UINT16 (buffer + 0);
  decoder->height = RFB_GET_UINT16 (buffer + 2);
  decoder->bpp = RFB_GET_UINT8 (buffer + 4);
  decoder->depth = RFB_GET_UINT8 (buffer + 5);
  decoder->big_endian = RFB_GET_UINT8 (buffer + 6);
  decoder->true_colour = RFB_GET_UINT8 (buffer + 7);
  decoder->red_max = RFB_GET_UINT16 (buffer + 8);
  decoder->green_max = RFB_GET_UINT16 (buffer + 10);
  decoder->blue_max = RFB_GET_UINT16 (buffer + 12);
  decoder->red_shift = RFB_GET_UINT8 (buffer + 14);
  decoder->green_shift = RFB_GET_UINT8 (buffer + 15);
  decoder->blue_shift = RFB_GET_UINT8 (buffer + 16);

  GST_DEBUG ("Server Initialization");
  GST_DEBUG ("width      = %d", decoder->width);
  GST_DEBUG ("height     = %d", decoder->height);
  GST_DEBUG ("bpp        = %d", decoder->bpp);
  GST_DEBUG ("depth      = %d", decoder->depth);
  GST_DEBUG ("big_endian = %d", decoder->big_endian);
  GST_DEBUG ("true_colour= %d", decoder->true_colour);
  GST_DEBUG ("red_max    = %d", decoder->red_max);
  GST_DEBUG ("green_max  = %d", decoder->green_max);
  GST_DEBUG ("blue_max   = %d", decoder->blue_max);
  GST_DEBUG ("red_shift  = %d", decoder->red_shift);
  GST_DEBUG ("green_shift= %d", decoder->green_shift);
  GST_DEBUG ("blue_shift = %d", decoder->blue_shift);

  name_length = RFB_GET_UINT32 (buffer + 20);

  buffer = rfb_decoder_read (decoder, name_length);

  decoder->name = g_strndup ((gchar *) (buffer), name_length);
  g_free (buffer);
  GST_DEBUG ("name       = %s", decoder->name);

  /* check if we need cropping */

  if (decoder->offset_x > 0) {
    if (decoder->offset_x > decoder->width) {
      GST_WARNING ("Trying to crop more than the width of the server");
    } else {
      decoder->width -= decoder->offset_x;
    }
  }
  if (decoder->offset_y > 0) {
    if (decoder->offset_y > decoder->height) {
      GST_WARNING ("Trying to crop more than the height of the server");
    } else {
      decoder->height -= decoder->offset_y;
    }
  }
  if (decoder->rect_width > 0) {
    if (decoder->rect_width > decoder->width) {
      GST_WARNING ("Trying to crop more than the width of the server");
    } else {
      decoder->width = decoder->rect_width;
    }
  }
  if (decoder->rect_height > 0) {
    if (decoder->rect_height > decoder->height) {
      GST_WARNING ("Trying to crop more than the height of the server");
    } else {
      decoder->height = decoder->rect_height;
    }
  }

  decoder->state = rfb_decoder_state_set_encodings;
  decoder->inited = TRUE;

  return TRUE;
}

static gboolean
rfb_decoder_state_normal (RfbDecoder * decoder)
{
  guint8 *buffer;
  gint message_type;

  buffer = rfb_decoder_read (decoder, 1);
  message_type = RFB_GET_UINT8 (buffer);

  switch (message_type) {
    case 0:
      GST_DEBUG ("Receiving framebuffer update");
      decoder->state = rfb_decoder_state_framebuffer_update;
      break;
    case 1:
      decoder->state = rfb_decoder_state_set_colour_map_entries;
      break;
    case 2:
      /* bell, ignored */
      decoder->state = rfb_decoder_state_normal;
      break;
    case 3:
      decoder->state = rfb_decoder_state_server_cut_text;
      break;
    default:
      g_critical ("unknown message type %d", message_type);
  }

  g_free (buffer);
  return TRUE;
}

static gboolean
rfb_decoder_state_framebuffer_update (RfbDecoder * decoder)
{
  guint8 *buffer;

  buffer = rfb_decoder_read (decoder, 3);

  decoder->n_rects = RFB_GET_UINT16 (buffer + 1);
  GST_DEBUG ("Number of rectangles : %d", decoder->n_rects);

  decoder->state = rfb_decoder_state_framebuffer_update_rectangle;

  return TRUE;
}

/*
static gboolean
rfb_decoder_state_framebuffer_update (RfbDecoder *decoder)
{
    RfbBuffer *buffer;
    gint ret;
    gint x, y, w, h;
    gint encoding;
    gint size;

  ret = rfb_bytestream_peek (decoder->bytestream, &buffer, 12);
  if (ret < 12)
    return FALSE;

  x = RFB_GET_UINT16 (buffer->data + 0);
  y = RFB_GET_UINT16 (buffer->data + 2);
  w = RFB_GET_UINT16 (buffer->data + 4);
  h = RFB_GET_UINT16 (buffer->data + 6);
  encoding = RFB_GET_UINT32 (buffer->data + 8);

  GST_DEBUG(" UPDATE Receiver");
  GST_DEBUG("x:%d y:%d", x, y);
  GST_DEBUG("w:%d h:%d", w, h);
  GST_DEBUG("encoding: %d", encoding);

  switch (encoding)
  {
    default:
        GST_WARNING("encoding type(%d) is not supported", encoding);
        return FALSE;
  }
  return TRUE;
}
*/
static gboolean
rfb_decoder_state_framebuffer_update_rectangle (RfbDecoder * decoder)
{
  guint8 *buffer;
  gint x, y, w, h;
  gint encoding;
  gint size;

  buffer = rfb_decoder_read (decoder, 12);

  x = RFB_GET_UINT16 (buffer + 0) - decoder->offset_x;
  y = RFB_GET_UINT16 (buffer + 2) - decoder->offset_y;
  w = RFB_GET_UINT16 (buffer + 4);
  h = RFB_GET_UINT16 (buffer + 6);
  encoding = RFB_GET_UINT32 (buffer + 8);

  GST_DEBUG ("update recieved");
  GST_DEBUG ("x:%d y:%d", x, y);
  GST_DEBUG ("w:%d h:%d", w, h);
  GST_DEBUG ("encoding: %d", encoding);

  if (encoding != 0)
    g_critical ("unimplemented encoding\n");

  g_free (buffer);

  size = w * h * decoder->bpp / 8;
  GST_DEBUG ("Reading %d bytes", size);
  buffer = rfb_decoder_read (decoder, size);
  GST_DEBUG ("Reading %d bytes", size);

  if (decoder->paint_rect) {
    decoder->paint_rect (decoder, x, y, w, h, buffer);
  }

  g_free (buffer);
  decoder->n_rects--;
  if (decoder->n_rects == 0) {
    decoder->state = rfb_decoder_state_normal;
  }
  return TRUE;
}

static gboolean
rfb_decoder_state_set_colour_map_entries (RfbDecoder * decoder)
{
  g_critical ("not implemented");

  return FALSE;
}

static gboolean
rfb_decoder_state_server_cut_text (RfbDecoder * decoder)
{
  g_critical ("not implemented");

  return FALSE;
}
