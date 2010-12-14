/**************************************************************************
*
* Copyright (C) 2005 Steve Karg <skarg@users.sourceforge.net>
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
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "config.h"
#include "bacdef.h"
#include "bacdcode.h"
#include "bacerror.h"
#include "bacdevobjpropref.h"
#include "apdu.h"
#include "npdu.h"
#include "abort.h"
#include "reject.h"
#include "rp.h"
/* device object has custom handler for all objects */
#include "device.h"
#include "session.h"
#include "handlers.h"
#include "handlers-data-core.h"

/** @file h_rp.c  Handles Read Property requests. */


/** Handler for a ReadProperty Service request.
 * @ingroup DSRP
 * This handler will be invoked by apdu_handler() if it has been enabled
 * by a call to apdu_set_confirmed_handler().
 * This handler builds a response packet, which is
 * - an Abort if
 *   - the message is segmented
 *   - if decoding fails
 *   - if the response would be too large
 * - the result from Device_Read_Property(), if it succeeds
 * - an Error if Device_Read_Property() fails 
 *   or there isn't enough room in the APDU to fit the data.
 * 
 * @param service_request [in] The contents of the service request.
 * @param service_len [in] The length of the service_request.
 * @param src [in] BACNET_ADDRESS of the source of the message
 * @param service_data [in] The BACNET_CONFIRMED_SERVICE_DATA information 
 *                          decoded from the APDU header of this message. 
 */
void handler_read_property(
    struct bacnet_session_object *sess,
    uint8_t * service_request,
    uint16_t service_len,
    BACNET_ADDRESS * src,
    BACNET_CONFIRMED_SERVICE_DATA * service_data)
{
    BACNET_READ_PROPERTY_DATA rpdata;
    int len = 0;
    int pdu_len = 0;
    int apdu_len = -1;
    int npdu_len = -1;
    BACNET_NPDU_DATA npdu_data;
    BACNET_APDU_FIXED_HEADER apdu_fixed_header;

    bool error = true;  /* assume that there is an error */
    int bytes_sent = 0;
    BACNET_ADDRESS my_address;
    uint8_t Handler_Transmit_Buffer[MAX_PDU_SEND] = { 0 };

    /* encode the NPDU portion of the packet */
    npdu_encode_npdu_data(&npdu_data, false, MESSAGE_PRIORITY_NORMAL);

    len = rp_decode_service_request(service_request, service_len, &rpdata);
#if PRINT_ENABLED
    if (len <= 0) {
        fprintf(stderr, "RP: Unable to decode Request!\n");
    }
#endif
    if (len < 0) {
        /* bad decoding - skip to error/reject/abort handling */
        error = true;
#if PRINT_ENABLED
        fprintf(stderr, "RP: Bad Encoding.\n");
#endif
        goto RP_FAILURE;
    }

    apdu_init_fixed_header(&apdu_fixed_header, PDU_TYPE_COMPLEX_ACK,
        service_data->invoke_id, SERVICE_CONFIRMED_READ_PROPERTY,
        service_data->max_resp);
    apdu_len =
        rp_ack_encode_apdu_init(&Handler_Transmit_Buffer[pdu_len],
        service_data->invoke_id, &rpdata);
    pdu_len += apdu_len;
    /* configure our storage */
    rpdata.application_data = &Handler_Transmit_Buffer[pdu_len];
    rpdata.application_data_len =
        sizeof(Handler_Transmit_Buffer) - pdu_len -
        1 /*HACK: -1 for closing tag */ ;
    len = Device_Read_Property(sess, &rpdata);
    if (len >= 0) {
        pdu_len += len;
        len =
            rp_ack_encode_apdu_object_property_end(&Handler_Transmit_Buffer
            [pdu_len]);
        pdu_len += len;
        /* begin sending all data, if possible */
        bytes_sent =
            tsm_set_complexack_transaction(sess, src, &npdu_data,
            &apdu_fixed_header, service_data, &Handler_Transmit_Buffer[0],
            pdu_len);
        error = false;
    }

  RP_FAILURE:
    if (error) {
        /* explicitely send error messages */
        sess->datalink_get_my_address(sess, &my_address);
        npdu_len =
            npdu_encode_pdu(&Handler_Transmit_Buffer[0], src, &my_address,
            &npdu_data);
        if (len == BACNET_STATUS_ABORT) {
            /* Kludge alert! At the moment we assume any abort is due to 
             * to space issues due to segmentation or lack thereof. I wanted to show the proper
             * handling via the abort_convert_error_code() so I put the error code
             * in here, if you are sure all aborts properly set up the error_code then
             * remove this next line
             */
            rpdata.error_code = ERROR_CODE_ABORT_SEGMENTATION_NOT_SUPPORTED;
            apdu_len =
                abort_encode_apdu(&Handler_Transmit_Buffer[npdu_len],
                service_data->invoke_id,
                abort_convert_error_code(rpdata.error_code), true);
#if PRINT_ENABLED
            fprintf(stderr, "RP: Sending Abort!\n");
#endif
        } else if (len == BACNET_STATUS_ERROR) {
            apdu_len =
                bacerror_encode_apdu(&Handler_Transmit_Buffer[npdu_len],
                service_data->invoke_id, SERVICE_CONFIRMED_READ_PROPERTY,
                rpdata.error_class, rpdata.error_code);
#if PRINT_ENABLED
            fprintf(stderr, "RP: Sending Error!\n");
#endif
        } else if (len == BACNET_STATUS_REJECT) {
            apdu_len =
                reject_encode_apdu(&Handler_Transmit_Buffer[npdu_len],
                service_data->invoke_id,
                reject_convert_error_code(rpdata.error_code));
#if PRINT_ENABLED
            fprintf(stderr, "RP: Sending Reject!\n");
#endif
        }
        pdu_len = npdu_len + apdu_len;
        bytes_sent =
            sess->datalink_send_pdu(sess, src, &npdu_data,
            &Handler_Transmit_Buffer[0], pdu_len);
#if PRINT_ENABLED
        if (bytes_sent <= 0)
            fprintf(stderr, "Failed to send Error PDU (%s)!\n",
                strerror(errno));
#endif
    }

    return;
}
