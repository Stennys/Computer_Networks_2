#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
#include "gbn.h"

/* ******************************************************************
   Go Back N protocol.  Adapted from J.F.Kurose
   ALTERNATING BIT AND GO-BACK-N NETWORK EMULATOR: VERSION 1.2  

   Network properties:
   - one way network delay averages five time units (longer if there
   are other messages in the channel for GBN), but can be larger
   - packets can be corrupted (either the header or the data portion)
   or lost, according to user-defined probabilities
   - packets will be delivered in the order in which they were sent
   (although some can be lost).

   Modifications: 
   - removed bidirectional GBN code and other code not used by prac. 
   - fixed C style to adhere to current programming style
   - added GBN implementation
**********************************************************************/

#define RTT  16.0       /* round trip time.  MUST BE SET TO 16.0 when submitting assignment */
#define WINDOWSIZE 6    /* the maximum number of buffered unacked packet */
#define SEQSPACE 12      /* the min sequence space for GBN must be at least windowsize + 1 */
#define NOTINUSE (-1)   /* used to fill header fields that are not being used */

/* generic procedure to compute the checksum of a packet.  Used by both sender and receiver  
   the simulator will overwrite part of your packet with 'z's.  It will not overwrite your 
   original checksum.  This procedure must generate a different checksum to the original if
   the packet is corrupted.
*/
int ComputeChecksum(struct pkt packet)
{
  int checksum = 0;
  int i;

  checksum = packet.seqnum;
  checksum += packet.acknum;
  for ( i=0; i<20; i++ ) 
    checksum += (int)(packet.payload[i]);

  return checksum;
}

bool IsCorrupted(struct pkt packet)
{
  if (packet.checksum == ComputeChecksum(packet))
    return (false);
  else
    return (true);
}


/********* Sender (A) variables and functions ************/

static struct pkt buffer[WINDOWSIZE];  /* array for storing packets waiting for ACK */
static int windowfirst, windowlast;    /* array indexes of the first/last packet awaiting ACK */
static int windowcount;                /* the number of packets currently awaiting an ACK */
static int A_nextseqnum;               /* the next sequence number to be used by the sender */

/*need for sr implementation*/
static int ackcount = 0;

/* called from layer 5 (application layer), passed the message to be sent to other side */
void A_output(struct msg message)
{
  struct pkt sendpkt;
  
  int i;


  /* if not blocked waiting on ACK */
  if (windowcount + ackcount < WINDOWSIZE) 
  {
    if (TRACE > 1)
      printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");

    /* create packet */
    sendpkt.seqnum = A_nextseqnum;
    sendpkt.acknum = NOTINUSE;
    for ( i=0; i<20 ; i++) 
      sendpkt.payload[i] = message.data[i];
    sendpkt.checksum = ComputeChecksum(sendpkt); 

    /* put packet in window buffer */
    /* windowlast will always be 0 for alternating bit; but not for GoBackN */    
    windowlast = (windowlast + 1) % WINDOWSIZE;
    buffer[windowlast] = sendpkt;
    windowcount++;
    

    /* send out packet */
    if (TRACE > 0)
      printf("Sending packet %d to layer 3\n", sendpkt.seqnum);
    tolayer3 (A, sendpkt);

    /* start timer if first packet in window */
    if (windowcount == 1)
      starttimer(A,RTT);

    /* get next sequence number, wrap back to 0 */
    A_nextseqnum = (A_nextseqnum + 1) % SEQSPACE;  
  }
  /* if blocked,  window is full */
  else {
    if (TRACE > 0)
      printf("----A: New message arrives, send window is full\n");
    window_full++;
  }
}


/* called from layer 3, when a packet arrives for layer 4 
   In this practical this will always be an ACK as B never sends data.
*/
void A_input(struct pkt packet)
{
  int i;

 
  /* if received ACK is not corrupted */ 
  if (!IsCorrupted(packet)) 
  {
    if (TRACE > 0)
      printf("----A: uncorrupted ACK %d is received\n",packet.acknum);
    total_ACKs_received++;

    /* check if new ACK or duplicate */
    if (windowcount != 0) 
    {
          int ack_num = packet.acknum;
          int seqfirst = buffer[windowfirst].seqnum;
          int seqlast = buffer[windowlast].seqnum;
          /* check case when seqnum has and hasn't wrapped */
          if (((seqfirst <= seqlast) && (packet.acknum >= seqfirst && packet.acknum <= seqlast)) ||
              ((seqfirst > seqlast) && (packet.acknum >= seqfirst || packet.acknum <= seqlast))) 
        {

            /* packet is a new ACK */
            if (TRACE > 0)
              printf("----A: ACK %d is not a duplicate\n",packet.acknum);
            /*NEW ACK mark as ture*/

            buffer[packet.acknum % WINDOWSIZE].acknum = 1;

            windowcount--;

            ackcount++;

            new_ACKs++;

            if (buffer[windowfirst].seqnum == packet.acknum)
            {

              for (i = 0; i < WINDOWSIZE; i++)
              {
                /*check to see if start has been aked*/

                if (buffer[windowfirst].acknum == 1)
                {

                windowfirst = (windowfirst + 1) % WINDOWSIZE;

                ackcount--;
                }

              }
                     
            stoptimer(A);
            if (windowcount > 0)
             {
              starttimer(A, RTT); 
            }
          }
        }
        else
        if (TRACE > 0)
          printf ("----A: duplicate ACK received, do nothing!\n");
      }
    }
  else 
  {
    if (TRACE > 0)
      printf ("----A: corrupted ACK is received, do nothing!\n");
  }
}

/* called when A's timer goes off */
void A_timerinterrupt(void)
{

  int i;

  if (TRACE > 0)
  printf("----A: time out,resend packets!\n");

  if (windowcount > 0)
  {
    for (i = 0; i < WINDOWSIZE; i++)
    {
      if (buffer[(i + windowfirst)%WINDOWSIZE].acknum != 1)
      {
        if (TRACE > 0)
          printf ("---A: resending packet %d\n", (buffer[(windowfirst + i) % WINDOWSIZE]).seqnum);
      tolayer3(A,buffer[(windowfirst + i) % WINDOWSIZE]);
      packets_resent++;
      starttimer(A,RTT);
      /*Will be the oldest*/
      break;
      }
    }
  }
}       



/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization */
void A_init(void)
{
  /* initialise A's window, buffer and sequence number */
  int i;
  A_nextseqnum = 0;  /* A starts with seq num 0, do not change this */
  windowfirst = 0;
  windowlast = -1;   /* windowlast is where the last packet sent is stored.  
		     new packets are placed in winlast + 1 
		     so initially this is set to -1
		   */
  windowcount = 0;

  
}



/********* Receiver (B)  variables and procedures ************/

static int expectedseqnum; /* the sequence number expected next by the receiver */
static int B_nextseqnum;   /* the sequence number for the next packets sent by B */
static struct pkt rcvBuffer[WINDOWSIZE];
static int bWindowStart;


/* called from layer 3, when a packet arrives for layer 4 at B*/
void B_input(struct pkt packet)
{
  struct pkt sendpkt;
  int i;
  int packet_del = 0;
  int buffer_idx = 0;


  /* if not corrupted and received packet can be in any order buffer it */
  if  ((!IsCorrupted(packet))) 
  {

    if (TRACE > 0)
      printf("----B: packet %d is correctly received, send ACK!\n",packet.seqnum);
    
    sendpkt.acknum = packet.seqnum; 
      /* we don't have any data to send.  fill payload with 0's */
    for (i=0; i<20 ; i++ ) 
        sendpkt.payload[i] = '0';  

    sendpkt.seqnum =  B_nextseqnum;

    B_nextseqnum = (B_nextseqnum + 1) % SEQSPACE;

        /* send an ACK for the received packet */


    /* computer checksum */
    sendpkt.checksum = ComputeChecksum(sendpkt); 

    packets_received++;
  
    /* send out packet */
    tolayer3 (B, sendpkt);

    int buffer_idx = (expectedseqnum - 1 + WINDOWSIZE) % SEQSPACE;

    int seq = packet.seqnum;

    int window_start = expectedseqnum;

    int window_end = (expectedseqnum + WINDOWSIZE) % SEQSPACE;
    
    bool in_window = (((expectedseqnum <= buffer_idx) && (packet.seqnum >= expectedseqnum && packet.seqnum <= buffer_idx)) ||
    ((expectedseqnum > buffer_idx) && (packet.seqnum >= expectedseqnum || packet.seqnum <= buffer_idx)));



    if (in_window){

    /*Check to see if packet was previously recieved*/ 

      rcvBuffer[seq % WINDOWSIZE] = packet;
  
      if (packet.seqnum == expectedseqnum)
      {
        for (i = 0; i < WINDOWSIZE; i++)
        {
          if (rcvBuffer[bWindowStart].seqnum == expectedseqnum)
          {
            tolayer5(B, rcvBuffer[windowfirst].payload);
            bWindowStart = (bWindowStart + 1) %WINDOWSIZE;
            expectedseqnum = (expectedseqnum + 1) % SEQSPACE;
          }
        }
      }
    }
  }
}

 

/* the following routine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization */
void B_init(void)
{
  int i = 0;
  expectedseqnum = 0;
  B_nextseqnum = 1;
  bWindowStart = 0;

}

/******************************************************************************
 * The following functions need be completed only for bi-directional messages *
 *****************************************************************************/

/* Note that with simplex transfer from a-to-B, there is no B_output() */
void B_output(struct msg message)  
{
}

/* called when B's timer goes off */
void B_timerinterrupt(void)
{
}

