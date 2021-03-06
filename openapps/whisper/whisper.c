/**
\brief Whisper module, runs on the (DAG)root only and is responsible for sending fake dio's
*/

#include <stdarg.h>

#include "opendefs.h"
#include "whisper.h"
#include "opencoap.h"
#include "openqueue.h"
#include "packetfunctions.h"
#include "openserial.h"
#include "openrandom.h"
#include "board.h"
#include "idmanager.h"
#include "msf.h"
#include "neighbors.h"
#include "schedule.h"
#include "IEEE802154E.h"
#include "scheduler.h"
#include "sixtop.h"
#include "leds.h"
#include "icmpv6rpl.h"

//=========================== defines =========================================

const uint8_t whisper_path0[] = "w";

//=========================== variables =======================================

whisper_vars_t whisper_vars;

//=========================== prototypes ======================================

owerror_t whisper_receive(OpenQueueEntry_t* msg,
						coap_header_iht*  coap_header,
						coap_option_iht*  coap_incomingOptions,
						coap_option_iht*  coap_outgoingOptions,
						uint8_t*          coap_outgoingOptionsLen);

void whisper_sendDone(OpenQueueEntry_t* msg, owerror_t error);

void whisper_timer_cb(opentimers_id_t id);

//=========================== public ==========================================

/**
\brief Initialize this module.
*/
void whisper_init() {
	//this will be run in the root
	if(!idmanager_getIsDAGroot()) return;
	whisper_log("Initializing whisper root node.\n");

	// prepare the resource descriptor for the /w path
	whisper_vars.desc.path0len             = sizeof(whisper_path0)-1;
	whisper_vars.desc.path0val             = (uint8_t*)(&whisper_path0);
	whisper_vars.desc.path1len             = 0;
	whisper_vars.desc.path1val             = NULL;
	whisper_vars.desc.componentID          = COMPONENT_WHISPER;
	whisper_vars.desc.discoverable         = TRUE;
	whisper_vars.desc.callbackRx           = &whisper_receive;
	whisper_vars.desc.callbackSendDone     = &whisper_sendDone;

        whisper_setState(0);

	// register with the CoAP module
	opencoap_register(&whisper_vars.desc);
}

void whisper_setState(uint8_t i) {
	whisper_vars.state=i;
}

uint8_t whisper_getState() {
	return whisper_vars.state;
}

open_addr_t* whisper_getTargetParentAddress(void) {
	return &whisper_vars.whisperParentTarget;
}

open_addr_t* whisper_getTargetAddress(void) {
	return &whisper_vars.whisperDioTarget;
}

open_addr_t* whisper_getNextHopRoot(void) {
    return &whisper_vars.whipserNextHopRoot;
}

//=========================== private =========================================
//not used, the root receives the primitives from Serial
owerror_t whisper_receive(OpenQueueEntry_t* msg,
						coap_header_iht*  coap_header,
						coap_option_iht*  coap_incomingOptions,
						coap_option_iht*  coap_outgoingOptions,
						uint8_t*          coap_outgoingOptionsLen)
{
	owerror_t outcome;
	switch (coap_header->Code) {
		case COAP_CODE_REQ_GET:
			// To reuse the packetBuffer we need to reset the message payload
			msg->payload = &(msg->packet[127]);
			msg->length = 0;

			// Create data and
			const int8_t respose_payload[] = "Whisper Loaded.";
			packetfunctions_reserveHeaderSize(msg,sizeof(respose_payload)-1);
			memcpy(&msg->payload[0],&respose_payload,sizeof(respose_payload)-1);

			// Set COAP header code
			coap_header->Code = COAP_CODE_RESP_CONTENT;
			outcome = E_SUCCESS;
			break;
	    case COAP_CODE_REQ_POST:
	        outcome = E_SUCCESS;
            break;
		default:
			outcome = E_FAIL;
			break;
	}

	return outcome;
}

void whisper_sendDone(OpenQueueEntry_t* msg, owerror_t error) {
	openqueue_freePacketBuffer(msg);
}

void whisper_task_remote(uint8_t* buf, uint8_t bufLen) {
    leds_sync_toggle();
	//will be run only in the root
    whisper_log("Recieved whisper command (serial).\n");
	if(!idmanager_getIsDAGroot()) {
        whisper_log("Not DAG root, not running whipser task.\n");
		return;
	}

	open_addr_t my_addr;
	my_addr.type = ADDR_128B;
	memcpy(&my_addr.addr_128b, 		idmanager_getMyID(ADDR_PREFIX)->prefix, 8);
	memcpy(&my_addr.addr_128b[8], 	idmanager_getMyID(ADDR_64B)->addr_64b, 8);

	switch(buf[1]) {
	    case 0x01:
            whisper_log("Whisper fake dio command (root)\n");

            // Target
            my_addr.addr_128b[14] = buf[2];
            my_addr.addr_128b[15] = buf[3];
            memcpy(&whisper_vars.whisperDioTarget, &my_addr, sizeof(open_addr_t));

            // Parent
            my_addr.addr_128b[14] = buf[4];
            my_addr.addr_128b[15] = buf[5];
            memcpy(&whisper_vars.whisperParentTarget, &my_addr, sizeof(open_addr_t));

            // Next Hop
            my_addr.addr_128b[14] = buf[6];
            my_addr.addr_128b[15] = buf[7];
            memcpy(&whisper_vars.whipserNextHopRoot, &my_addr, sizeof(open_addr_t));

            // Rank, rank always last to bytes of the buffer
            dagrank_t rank = (uint16_t) ((uint16_t) buf[bufLen - 1] << 8) | (uint16_t ) buf[bufLen];

            whisper_log("Sending fake DIO with rank %d.\n", rank);

            uint8_t result = send_WhisperDIO(rank);

            uint8_t data[2];
            data[0] = 0x01; // indicate fake dio send from root
            data[1] = result;
            openserial_sendWhisper(data, 2);

            break;
	    case 0x02:
	        whisper_log("Sending fake DIS message.");
	        // Create and send COAP PUT packet to whisper node
        default:
            whisper_log("Received wrong command\n");
            break;
	}
}

void whisper_log(char* msg, ...) {
/*	open_addr_t* my_id = idmanager_getMyID(ADDR_16B);*/
/*	printf("[%d] Whisper: \t", my_id->addr_64b[1]);*/

/*	char buf[100];*/
/*	va_list v1;*/
/*	va_start(v1, msg);*/
/*	vsnprintf(buf, sizeof(buf), msg, v1);*/
/*	va_end(v1);*/

/*	printf(buf);*/
}

void whisper_print_address(open_addr_t* addr) {
/*	uint8_t length = 4;*/
/*	uint8_t* start_addr = addr->addr_16b;*/
/*	switch (addr->type) {*/
/*		case ADDR_64B:*/
/*			length = 8;*/
/*			start_addr = addr->addr_64b;*/
/*			break;*/
/*		case ADDR_128B:*/
/*			length = 16;*/
/*			start_addr = addr->addr_128b;*/
/*			break;*/
/*		default: break;*/
/*	}*/

/*	for(uint8_t i = 0; i < length; i++) {*/
/*		printf("%02x", start_addr[i]);*/
/*		if(i < length - 1) printf(":");*/
/*	}*/
/*	printf("\n");*/
}



