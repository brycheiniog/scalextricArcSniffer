#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "radio_config.h"
#include "nrf_gpio.h"
#include "boards.h"
#include "bsp.h"
#include "bsp_config.h"
#include "app_timer.h"
#include "nrf_drv_clock.h"
#include "nordic_common.h"
#include "nrf_error.h"
#include "app_error.h"
 #define NRF_LOG_MODULE_NAME "APP"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "radio_config.h"
#include "nrf_delay.h"

/* Definitions */

#define APP_TIMER_PRESCALER      0                    // Value of the RTC1 PRESCALER register.
#define APP_TIMER_OP_QUEUE_SIZE  2                    // Size of timer operation queues.
#define APP_TIMER_TICKS_TIMEOUT  0x7fffffff
#define MAX_PACKET_SIZE          10                   // Maximum size of a received packet
#define PKT_CIRCULAR_BUF_SIZE    30                   //Number of packets to hold in circular buffer
#define FREQ_BANDS               14                   //Define the number of frequency bands. 
                                                      //There are a total of 14 bands in use but channel 81 
                                                      //is only used for pairing
#define WAIT_TIME                34                   //The maximum amount of time to wait for a second packet on a channel
#define MAX_STRING_LENGTH        100    

#define LOG_LANE                 1                    //Number of the lane to log
#define LOG_UNKNOWN              1                    //Log the unknown channel (79)
#define LOG_PAIRING              0                    //Log the pairing channel (81)

/* Types */
typedef struct  {                                         //Structure for the packet store
    uint8_t                   packet[MAX_PACKET_SIZE];    //Packet buffer
    int32_t                   time;                       //Offset to the last recieved packet
    uint8_t                   CRCStatus;                  //CRC status of the packet
    uint8_t                   frequency;                  //Channel the packet was captured on
} packet_s;

typedef struct {                                          //Structure mapping 
    uint8_t                   frequency;                  //Frequency 
    uint8_t                   lane;                       //Lane associated with this frequency
    uint8_t                   function;                   //Function of the channel
} channel_s;

enum channelFunction {PRIMARY_CHANNEL,SECONDARY_CHANNEL,UNKNOWN,PAIRING};


/* Global variables */

static packet_s               rxPacketStore[PKT_CIRCULAR_BUF_SIZE];   //Circular buffer to store received packets & metadata
static uint32_t               writeIdx;                               //Packet buffer write index
static uint32_t               readIdx;                                //Packet buffer read index
static uint32_t               pktCount;

       channel_s              channels[FREQ_BANDS] = {                            //Channel to frequency map
                                          {5,1,PRIMARY_CHANNEL},
                                          {11,2,PRIMARY_CHANNEL},
                                          {17,3,PRIMARY_CHANNEL},
                                          {23,4,PRIMARY_CHANNEL},
                                          {29,5,PRIMARY_CHANNEL},
                                          {35,6,PRIMARY_CHANNEL},
                                          {41,1,SECONDARY_CHANNEL},
                                          {47,2,SECONDARY_CHANNEL},
                                          {53,3,SECONDARY_CHANNEL},
                                          {59,4,SECONDARY_CHANNEL},
                                          {65,5,SECONDARY_CHANNEL},
                                          {71,6,SECONDARY_CHANNEL},
                                          {79,0xff,UNKNOWN},
                                          {81,0xff,PAIRING}
                                         };
static uint16_t freqIdx;
static uint32_t intTime;
       uint32_t irqCounter;
       uint32_t lastTime;
static bool     updateRadio;          

/* Function Prototypes */
       void        restartRadio(uint8_t freq);
       bool        checkFrequency(uint8_t frequency);
       int32_t     read_packet();
       void        startRadio();
static void        csense_timeout_handler(void * p_context);
       void        radio_configure_arc();
static void        init(void);



APP_TIMER_DEF(timer_0);
APP_TIMER_DEF(timer_1);

void clock_initialization()
{
    /* Start 16 MHz crystal oscillator */
    NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
    NRF_CLOCK->TASKS_HFCLKSTART    = 1;

    /* Wait for the external oscillator to start up */
    while (NRF_CLOCK->EVENTS_HFCLKSTARTED == 0)
    {
        // Do nothing.
    }

    /* Start low frequency crystal oscillator for app_timer(used by bsp)*/
    NRF_CLOCK->LFCLKSRC            = (CLOCK_LFCLKSRC_SRC_RC <<CLOCK_LFCLKSRC_SRC_Pos );
    NRF_CLOCK->EVENTS_LFCLKSTARTED = 0;
    NRF_CLOCK->TASKS_LFCLKSTART    = 1;

    while (NRF_CLOCK->EVENTS_LFCLKSTARTED == 0)
    {
        // Do nothing.
    }
}

void startRadio()
{

    NVIC_SetPriority(RADIO_IRQn, 1);
    NVIC_ClearPendingIRQ(RADIO_IRQn);
    NVIC_EnableIRQ(RADIO_IRQn);

    // Enable radio and wait for ready
    NRF_RADIO->INTENSET = NRF_RADIO->INTENSET | RADIO_INTENSET_END_Msk;
    NRF_RADIO->SHORTS   = 0x1;   //Enable the End->Start mask    
    NRF_RADIO->TASKS_RXEN = 1U;
}

static void csense_timeout_handler(void * p_context)
{
    return;
}

static void stopMonitoringHandler(void * p_context)
{
    ret_code_t err_code;

    intTime = app_timer_cnt_get();
    updateRadio = true;
    
    return;
}



static void init(void)
{
        int i,j;
        writeIdx = 0;
        readIdx = 0;

        for(i=0;i<PKT_CIRCULAR_BUF_SIZE;i++)
        {
          for(j=0;j<MAX_PACKET_SIZE;j++)
           rxPacketStore[i].packet[j]=0xDE;


            rxPacketStore[i].time = 0;
            rxPacketStore[i].CRCStatus = 0xDE;
        }
}




void RADIO_IRQHandler()
{
    uint32_t timerValue;
    uint32_t tmp;

    
    //Start the timeout for this frequency if this is the first packet.
     if(irqCounter == 0)
     {
         app_timer_start(timer_1, WAIT_TIME, NULL);
     }
     
     irqCounter++;
     

     if(NRF_RADIO->EVENTS_END && (NRF_RADIO->INTENSET & RADIO_INTENSET_END_Msk))
      {
          NRF_RADIO->EVENTS_END = 0;
      }

      //Update the packet store

      rxPacketStore[writeIdx].CRCStatus = NRF_RADIO->CRCSTATUS;
      timerValue = app_timer_cnt_get();

      app_timer_cnt_diff_compute(timerValue,lastTime,&tmp);
      rxPacketStore[writeIdx].time = tmp;

      lastTime = timerValue;
      rxPacketStore[writeIdx].frequency = NRF_RADIO->FREQUENCY;      

      pktCount++;     //Number of recieved packes
      

      //Update the circular buffer write index
      writeIdx++;

      if(writeIdx==PKT_CIRCULAR_BUF_SIZE)     //Wrap write index at end of circular buffer
        writeIdx = 0;

      if(writeIdx == readIdx)
        NRF_LOG_INFO("Circular buffer overrun\r\n");     //Shouldn't ever get here.
        
      //Update the address to the next entry in the store.
      NRF_RADIO->PACKETPTR = (uint32_t)&rxPacketStore[writeIdx].packet[0];
      //Start the radio again.
      NRF_RADIO->TASKS_START = 1U;
}


void radio_configure_arc()
{
    // Radio config
    NRF_RADIO->TXPOWER   = (RADIO_TXPOWER_TXPOWER_Pos4dBm << RADIO_TXPOWER_TXPOWER_Pos);
    NRF_RADIO->FREQUENCY = 6U;  // Frequency bin 7, 2407MHz
    NRF_RADIO->MODE = (RADIO_MODE_MODE_Nrf_250Kbit << RADIO_MODE_MODE_Pos);

    NRF_RADIO->PREFIX0 = 0x23c343d2;
    NRF_RADIO->PREFIX1 = 0x13e363a3;
    
    // Radio address config

    NRF_RADIO->BASE0 = 0x864e9676;  // Base address for prefix 0 converted to nRF24L series format
    NRF_RADIO->BASE1 = 0x43434343;  // Base address for prefix 1-7 converted to nRF24L series format


    NRF_RADIO->TXADDRESS   = 0x00UL;  // Set device address 0 to use when transmitting
    NRF_RADIO->RXADDRESSES = 0x03UL;  // Enable device address 0 to use to select which addresses to receive

    // Packet configuration

    NRF_RADIO->PCNF0 = 0x00030006;
    NRF_RADIO->PCNF1 = 0x01040020;

    //CRC Config

    NRF_RADIO->CRCCNF = 0x1;
    NRF_RADIO->CRCPOLY = 0x107;
    NRF_RADIO->CRCINIT = 0xff;
}

//Reconfigure the radio to listen on a different frequency
void restartRadio(uint8_t freq)
{

    NRF_RADIO->EVENTS_DISABLED = 0U;
    // Disable radio
    NRF_RADIO->TASKS_DISABLE = 1U;

    while (NRF_RADIO->EVENTS_DISABLED == 0U)
    {
        // wait
    }
    
    NRF_RADIO->FREQUENCY = freq;
    
    NRF_RADIO->TASKS_RXEN = 1U;
    
    NRF_RADIO->EVENTS_READY = 0U;
    // Start listening and wait for address received event
    NRF_RADIO->TASKS_START = 1U;  

}

bool checkFrequency(uint8_t frequency)
{
    uint16_t i;

    if((frequency == 79) && (LOG_UNKNOWN))
       return true;

    if((frequency == 81) && (LOG_PAIRING))
       return true;
    
    for(i=0;i<FREQ_BANDS;i++)
    {
      if((frequency == channels[i].frequency) && (LOG_LANE == channels[i].lane))
         return true;
    }
    return false;
}

int main(void)
{
    uint32_t err_code = NRF_SUCCESS;
    char str[MAX_STRING_LENGTH];

    //Initialise the rx packet store
    init();

    //Setup the board clocks
    clock_initialization();

    //Initialise the hardware timers.
    APP_TIMER_INIT(APP_TIMER_PRESCALER, APP_TIMER_OP_QUEUE_SIZE, NULL);
    //Create timer 0 so that app_timer_cnt_get() will work.
    err_code = app_timer_create(&timer_0, APP_TIMER_MODE_REPEATED, csense_timeout_handler);
    APP_ERROR_CHECK(err_code);
    //Create a timer which will be used to trigger the switch to the next channel after
    //the first packet is received on a channel.
    err_code = app_timer_create(&timer_1, APP_TIMER_MODE_SINGLE_SHOT, stopMonitoringHandler);
    APP_ERROR_CHECK(err_code);

    //Initialise the Logging module;
    err_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(err_code);

    //Init the LED, timer and so on.
    err_code = bsp_init(BSP_INIT_LED, APP_TIMER_TICKS(100, APP_TIMER_PRESCALER), NULL);
    APP_ERROR_CHECK(err_code);

    // Set radio configuration parameters
    radio_configure_arc();

    
    err_code = bsp_indication_set(BSP_INDICATE_USER_STATE_OFF);
    
    APP_ERROR_CHECK(err_code);
    NRF_LOG_FLUSH();    
    
    

    //Display the output header
    sprintf(str,"Elapsed Time,CRC Status,Frequency Bin,Pkt[0],Pkt[1],Pkt[2],Pkt[3],Pkt[4],Pkt[5]\r\n");
    NRF_LOG_RAW_INFO("%s",str);    


    //Setup
    freqIdx = 0;
    NRF_RADIO->FREQUENCY = channels[freqIdx].frequency;  
    pktCount = 0;
    NRF_RADIO->PACKETPTR = (uint32_t)&rxPacketStore[writeIdx].packet[0];
    
    //Start the radio
    startRadio();
    
    //Start the first timer
    app_timer_start(timer_0, APP_TIMER_TICKS_TIMEOUT, NULL); //Start long running timer so cnt_get() works
    lastTime = app_timer_cnt_get();

    while(pktCount == 0)          //Wait for a packet to arrive.
    {}
    
    //Loop forever
    while(1)
    {   
       //Check flag to see if the radio needs to be
       //Reconfigured for a new channel
       if(updateRadio == true)
       {                 
              //Move to the next channel
              freqIdx++;
              if(LOG_PAIRING==0)
              {
                if(freqIdx == FREQ_BANDS-1)
                {
                 freqIdx = 0;
                }
              } else {
                if(freqIdx == FREQ_BANDS)
                {
                 freqIdx = 0;
                }
              }
              //Restart the radio with the new frequency config
              restartRadio(channels[freqIdx].frequency);
              //Clear update flag
              updateRadio = false;
              //Reset the counter
              irqCounter=0;
       }
       
        //If there is something in the circular buffer then dump to the log
        if(readIdx!=writeIdx)
        {
          //sprintf(str,"%d,%d\r\n",rxPacketStore[readIdx].time,rxPacketStore[readIdx].frequency);
          //NRF_LOG_RAW_INFO("%s",str); 

            if(checkFrequency(rxPacketStore[readIdx].frequency))
            {
              sprintf(str,"%d,%d,%d,%x,%x,%x,%x,%x,%x\r\n",rxPacketStore[readIdx].time, rxPacketStore[readIdx].CRCStatus, rxPacketStore[readIdx].frequency, rxPacketStore[readIdx].packet[0],rxPacketStore[readIdx].packet[1],rxPacketStore[readIdx].packet[2],rxPacketStore[readIdx].packet[3],rxPacketStore[readIdx].packet[4],rxPacketStore[readIdx].packet[5]);
              NRF_LOG_RAW_INFO("%s",str);            
            }
            
            readIdx++;
            if(readIdx==PKT_CIRCULAR_BUF_SIZE)
               readIdx = 0;
        }               
    }
}


