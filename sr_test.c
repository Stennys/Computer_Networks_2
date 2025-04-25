#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
#include "gbn.h"

#define RTT 16.0
#define WINDOWSIZE 6
#define SEQSPACE 7
#define NOTINUSE (-1)

int ComputeChecksum(struct pkt packet) {
    int checksum = 0;
    int i;
    checksum += packet.seqnum + packet.acknum;
    for(i=0; i<20; i++)
        checksum += (int)packet.payload[i];
    return checksum;
}

bool IsCorrupted(struct pkt packet) {
    return packet.checksum != ComputeChecksum(packet);
}

/********** Sender (A) **********/
static struct pkt buffer[WINDOWSIZE];
static int send_base = 0;
static int next_seq = 0;
static bool acked[SEQSPACE] = {false};
static int window_count = 0;

void A_output(struct msg message) {
    if(window_count < WINDOWSIZE) {
        struct pkt pkt;
        pkt.seqnum = next_seq;
        pkt.acknum = NOTINUSE;
        int i;
        for(i=0; i<20; i++)
            pkt.payload[i] = message.data[i];
        pkt.checksum = ComputeChecksum(pkt);
        
        buffer[next_seq % WINDOWSIZE] = pkt;
        acked[next_seq] = false;
        window_count++;
        
        if(TRACE > 0) printf("Sending packet %d\n", next_seq);
        tolayer3(A, pkt);
        
        if(window_count == 1) starttimer(A, RTT);
            
        next_seq = (next_seq + 1) % SEQSPACE;
    } else {
        if(TRACE > 0) printf("----A: Window full\n");
    }
}

void A_input(struct pkt packet) {
    if(!IsCorrupted(packet)) {
        int ack = packet.acknum;
        int window_start = send_base;
        int window_end = (send_base + WINDOWSIZE) % SEQSPACE;
        
        bool in_window = (window_start <= window_end) ? 
            (ack >= window_start && ack < window_end) :
            (ack >= window_start || ack < window_end);
        
        if(in_window && !acked[ack]) {
            acked[ack] = true;
            if(TRACE > 0) printf("----A: ACK %d received\n", ack);
   
            while(acked[send_base] && window_count > 0) {
                acked[send_base] = false;
                send_base = (send_base + 1) % SEQSPACE;
                window_count--;
            }
            
           
            stoptimer(A);
            if(window_count > 0) starttimer(A, RTT);
        }
    }
}

void A_timerinterrupt() {
    if(TRACE > 0) printf("----A: Timeout, resending packet %d\n", send_base);
    tolayer3(A, buffer[send_base % WINDOWSIZE]);
    starttimer(A, RTT);
}

void A_init() {
    send_base = 0;
    next_seq = 0;
    window_count = 0;
    int i;
    for(i=0; i<SEQSPACE; i++) acked[i] = false;
}

/********** Receiver (B) **********/
static int expected_seq = 0;
static struct pkt rcv_buffer[SEQSPACE];

void B_input(struct pkt packet) {
    if(!IsCorrupted(packet)) {
        int seq = packet.seqnum;
        int window_start = expected_seq;
        int window_end = (expected_seq + WINDOWSIZE) % SEQSPACE;
        
        bool in_window = (window_start <= window_end) ?
            (seq >= window_start && seq < window_end) :
            (seq >= window_start || seq < window_end);
        
        if(in_window) {
            if(TRACE > 0) printf("----B: Received packet %d\n", seq);
            rcv_buffer[seq] = packet; 
            
          
            while(rcv_buffer[expected_seq].seqnum == expected_seq) {
                if(TRACE > 0) printf("----B: Delivering packet %d to layer5\n", expected_seq);
                tolayer5(B, rcv_buffer[expected_seq].payload);
                expected_seq = (expected_seq + 1) % SEQSPACE;
            }
        }
        
       
        struct pkt ack;
        ack.acknum = seq;
        ack.seqnum = NOTINUSE;
        int i;
        for(i=0; i<20; i++) ack.payload[i] = '0';
        ack.checksum = ComputeChecksum(ack);
        
        if(TRACE > 0) printf("----B: Sending ACK %d\n", seq);
        tolayer3(B, ack);
    }
}

void B_init() {
    expected_seq = 0;
    int i;
    for(i=0; i<SEQSPACE; i++) {
        rcv_buffer[i].seqnum = -1; 
    }
}

void B_output(struct msg message) {}
void B_timerinterrupt() {}