/**************************************************************************
*
* Copyright (C) 2006 Steve Karg <skarg@users.sourceforge.net>
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be included
* in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*
*********************************************************************/
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include "config.h"
#include "config.h"
#include "txbuf.h"
#include "bacdef.h"
#include "bacdcode.h"
#include "address.h"
#include "tsm.h"
#include "npdu.h"
#include "apdu.h"
#include "device.h"
#include "datalink.h"
#include "dcc.h"
#include "ihave.h"
/* some demo stuff needed */
#include "handlers.h"
#include "txbuf.h"

/* find a specific device, or use -1 for limit if you want unlimited */
void Send_I_Have(uint32_t device_id,
    BACNET_OBJECT_TYPE object_type,
    uint32_t object_instance, char *object_name)
{
    int pdu_len = 0;
    BACNET_ADDRESS dest;
    int bytes_sent = 0;
    BACNET_I_HAVE_DATA data;

    /* if we are forbidden to send, don't send! */
    if (!dcc_communication_enabled())
        return;
    /* Who-Has is a global broadcast */
    datalink_get_broadcast_address(&dest);
    /* encode the NPDU portion of the packet */
    pdu_len = npdu_encode_apdu(&Handler_Transmit_Buffer[0], &dest, NULL, false, /* true for confirmed messages */
        MESSAGE_PRIORITY_NORMAL);
    /* encode the APDU portion of the packet */
    data.device_id.type = OBJECT_DEVICE;
    data.device_id.instance = device_id;
    data.object_id.type = object_type;
    data.object_id.instance = object_instance;
    characterstring_init_ansi(&data.object_name, object_name);
    pdu_len += ihave_encode_apdu(&Handler_Transmit_Buffer[pdu_len], &data);
    /* send the data */
    bytes_sent = datalink_send_pdu(&dest,       // destination address
        &Handler_Transmit_Buffer[0], pdu_len);  // number of bytes of data
    if (bytes_sent <= 0)
        fprintf(stderr, "Failed to Send I-Have Reply (%s)!\n",
            strerror(errno));
}
