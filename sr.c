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
#define SEQSPACE 7      /* the min sequence space for GBN must be at least windowsize + 1 */
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
static double timesBuffer[WINDOWSIZE]; /*For checking if packet is expired aka need to resend*/
static bool ackedArray[WINDOWSIZE];
static double hardwareTimerVal;
static int hardwareTimerRunning;
/* called from layer 5 (application layer), passed the message to be sent to other side */
void A_output(struct msg message)
{
  struct pkt sendpkt;
  int i;

  /* if not blocked waiting on ACK */
  if ( windowcount < WINDOWSIZE) {
    if (TRACE > 1)
      printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");

    /* create packet */
    sendpkt.seqnum = A_nextseqnum;
    sendpkt.acknum = NOTINUSE;
    for ( i=0; i<20 ; i++ ) 
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
    timesBuffer[windowlast] = RTT;
    

    reset_hardware_timer();

    

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
  int ackcount = 0;
  int i;

  /* if received ACK is not corrupted */ 
  if (!IsCorrupted(packet)) {
    if (TRACE > 0)
      printf("----A: uncorrupted ACK %d is received\n",packet.acknum);
    total_ACKs_received++;

    /* check if new ACK or duplicate */
    if (windowcount != 0) {
          int seqfirst = buffer[windowfirst].seqnum;
          int seqlast = buffer[windowlast].seqnum;
          /* check case when seqnum has and hasn't wrapped */
          if (((seqfirst <= seqlast) && (packet.acknum >= seqfirst && packet.acknum <= seqlast)) ||
              ((seqfirst > seqlast) && (packet.acknum >= seqfirst || packet.acknum <= seqlast))) {


          if (windowcount > 0){
            reset_hardware_timer();
          }

            /* packet is a new ACK */
            if (TRACE > 0)
              printf("----A: ACK %d is not a duplicate\n",packet.acknum);
            new_ACKs++;

            /* Need to find which timer has to be stopped */
            int i;
            for (i = 0; i < WINDOWSIZE; i++){
              if (buffer[i].seqnum == packet.acknum) {
                timesBuffer[i] = -1;
                printf(">>> A_input() stopped timer for slot %d (seq=%d)\n", i, packet.acknum);
                break;
            }
          }


            /* cumulative acknowledgement - determine how many packets are ACKed */
            if (packet.acknum >= seqfirst)
              ackcount = packet.acknum + 1 - seqfirst;
            else
              ackcount = SEQSPACE - seqfirst + packet.acknum;

	    /* slide window by the number of packets ACKed */
            windowfirst = (windowfirst + ackcount) % WINDOWSIZE;

            /* delete the acked packets from window buffer */
            for (i=0; i<ackcount; i++)
              windowcount--;

	    /* start timer again if there are still more unacked packets in window */
            

      

          }
        }
        else
          if (TRACE > 0)
        printf ("----A: duplicate ACK received, do nothing!\n");
  }
  else 
    if (TRACE > 0)
      printf ("----A: corrupted ACK is received, do nothing!\n");
}

/* called when A's timer goes off */
void A_timerinterrupt(void)
{ 

  printf(">>> A_timerinterrupt fired (hwTimerVal=%f)\n", hardwareTimerVal);
  
  int i;
  double curr_time = hardwareTimerVal;
  printf(">>> [TIME: %.3f] A_timerinterrupt() called. Timer expired.\n", curr_time);

  if (TRACE > 0)
    printf("----A: time out,resend packet!\n");
  /* Sub all active hardware timers  */
  for(i=0; i<windowcount; i++) {
    if(timesBuffer[i] >= 0){
      timesBuffer[i] = timesBuffer[i] - curr_time;
    }
  }
  /*If hardware timer is less then zero or past then we need to retransmit*/
  for (i = 0; i < windowcount; i++){
    if(timesBuffer[i] <= 0){
      printf(">>> retransmitting slot %d (seq=%d)\n", i, buffer[i].seqnum);
      tolayer3(A,buffer[i]);
      timesBuffer[i] = RTT;
      packets_resent++;
    }
  }
    hardwareTimerRunning = 0;
    reset_hardware_timer();
  


    if (TRACE > 0)
      printf ("---A: resending packet %d\n", (buffer[(windowfirst+i) % WINDOWSIZE]).seqnum);

  }




/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization */
void A_init(void)
{
  /* initialise A's window, buffer and sequence number */
  A_nextseqnum = 0;  /* A starts with seq num 0, do not change this */
  windowfirst = 0;
  windowlast = -1;   /* windowlast is where the last packet sent is stored.  
		     new packets are placed in winlast + 1 
		     so initially this is set to -1
		   */
  windowcount = 0;
  int i;
  for (i = 0; i < WINDOWSIZE; i++) {
    /*Negative one represent not sent*/
    timesBuffer[i] = -1;
    
    ackedArray[i] = false;
  }
  hardwareTimerRunning = 0;
}



/********* Receiver (B)  variables and procedures ************/

static int expectedseqnum; /* the sequence number expected next by the receiver */
static int B_nextseqnum;   /* the sequence number for the next packets sent by B */


/* called from layer 3, when a packet arrives for layer 4 at B*/
void B_input(struct pkt packet)
{
  struct pkt sendpkt;
  int i;

  /* if not corrupted and received packet is in order */
  if  ( (!IsCorrupted(packet))  && (packet.seqnum == expectedseqnum) ) {
    if (TRACE > 0)
      printf("----B: packet %d is correctly received, send ACK!\n",packet.seqnum);
    packets_received++;

    /* deliver to receiving application */
    tolayer5(B, packet.payload);

    /* send an ACK for the received packet */
    sendpkt.acknum = expectedseqnum;

    /* update state variables */
    expectedseqnum = (expectedseqnum + 1) % SEQSPACE;        
  }
  else {
    /* packet is corrupted or out of order resend last ACK */
    if (TRACE > 0) 
      printf("----B: packet corrupted or not expected sequence number, resend ACK!\n");
    if (expectedseqnum == 0)
      sendpkt.acknum = SEQSPACE - 1;
    else
      sendpkt.acknum = expectedseqnum - 1;
  }

  /* create packet */
  sendpkt.seqnum = B_nextseqnum;
  B_nextseqnum = (B_nextseqnum + 1) % 2;
    
  /* we don't have any data to send.  fill payload with 0's */
  for ( i=0; i<20 ; i++ ) 
    sendpkt.payload[i] = '0';  

  /* computer checksum */
  sendpkt.checksum = ComputeChecksum(sendpkt); 

  /* send out packet */
  tolayer3 (B, sendpkt);
}

/* the following routine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization */
void B_init(void)
{
  expectedseqnum = 0;
  B_nextseqnum = 1;
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

/* Hardware timer helper functions*/

void reset_hardware_timer(void){


  if (hardwareTimerRunning){
    stoptimer(A);
    hardwareTimerRunning = 0;
  }

  /*find next timer to time out*/
  double next_timeout = timesBuffer[windowfirst];

  /* create and start hardware timer */
  if ((next_timeout < 1e3) && (windowcount > 0)){
    starttimer(A, next_timeout);
    hardwareTimerVal = next_timeout;
    hardwareTimerRunning = 1;
  }
}