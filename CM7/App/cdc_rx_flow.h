#ifndef SPOOKY_CDC_RX_FLOW_H
#define SPOOKY_CDC_RX_FLOW_H

/*
 * USB CDC command receive ring with flow control (8lw.24). The USB interrupt
 * pushes each OUT packet and re-arms the endpoint only while another full packet
 * still fits; otherwise reception pauses, the host is NAKed and keeps its data,
 * and the foreground re-arms once it has drained enough. No received byte is
 * dropped while the invariant holds; a packet that does not fit anyway is
 * truncated and counted as an overrun.
 *
 * Portable: one producer (the USB interrupt) and one consumer (the foreground).
 * The paused flag changes hands only while the endpoint is not armed, so the two
 * never race on it.
 */

#include <stdbool.h>
#include <stdint.h>

#define CDC_RX_FLOW_CAPACITY 256U /* Usable bytes: CAPACITY - 1. */

typedef struct CdcRxFlow
{
  uint8_t data[CDC_RX_FLOW_CAPACITY];
  volatile uint16_t head;     /* Written by the producer. */
  volatile uint16_t tail;     /* Written by the consumer. */
  volatile bool paused;       /* Endpoint left unarmed by the producer. */
  uint16_t packet_size;       /* Largest OUT packet the endpoint delivers. */
  volatile uint32_t packets;
  volatile uint32_t pauses;
  volatile uint32_t overruns; /* Packets truncated because they did not fit. */
} CdcRxFlow;

void CdcRxFlow_Init(CdcRxFlow *flow, uint16_t packet_size);
uint16_t CdcRxFlow_Free(const CdcRxFlow *flow);
/* Producer: copies one packet. Returns true when the endpoint may be re-armed
 * now; false leaves reception paused until CdcRxFlow_Resume says otherwise. */
bool CdcRxFlow_Push(CdcRxFlow *flow, const uint8_t *packet, uint32_t length);
/* Consumer: takes one byte. */
bool CdcRxFlow_Pop(CdcRxFlow *flow, uint8_t *byte);
/* Consumer: true once, when reception was paused and a full packet fits again;
 * the caller then re-arms the endpoint. */
bool CdcRxFlow_Resume(CdcRxFlow *flow);

#endif /* SPOOKY_CDC_RX_FLOW_H */
