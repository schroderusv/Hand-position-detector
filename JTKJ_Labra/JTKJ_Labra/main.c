//Johdatus tietokonejärjestelmiin lopputyö Elisa Juvonen ja Vappu Schroderus


#include <stdio.h>



/* XDCtools files */
#include <xdc/std.h>
#include <xdc/runtime/System.h>

/* BIOS Header files */
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/drivers/PIN.h>
#include <ti/drivers/pin/PINCC26XX.h>
#include <ti/drivers/I2C.h>
#include <ti/drivers/Power.h>
#include <ti/drivers/power/PowerCC26XX.h>
#include <ti/mw/display/Display.h>
#include <ti/mw/display/DisplayExt.h>
#include <ti/drivers/i2c/I2CCC26XX.h>
#include <ti/drivers/pin/PINCC26XX.h>

/* Board Header files */
#include "Board.h"
#include "sensors/mpu9250.h"
#include "wireless/comm_lib.h"
#include "sensors/bmp280.h"

//Äänentoisto
#include <driverlib/timer.h>
#include "buzzer.h"


//Näitä tarvitaan viestien vastaanottamisessa. Laitetaan globaaleiksi
char payload[16]; // viestipuskuri
uint16_t senderAddr;


// Määritellään mahdolliset tilat:
enum state { WAIT=1, RYTMIMUNA, LAPYT, MSG_WAITING, TUNNISTAMATON, READ_SENSOR, VASEN }; //WAIT=1
// Esitellään ja alustetaan globaali tilamuuttuja odotustilaan
enum state myState = WAIT;

/* Task */
#define STACKSIZE 2048
Char labTaskStack[STACKSIZE];
Char commTaskStack[STACKSIZE];
Char displayFxnStack[STACKSIZE];

/* Display */
Display_Handle hDisplay;


//mpu pinni globaaliks
static PIN_Handle hMpuPin;

// Painonappien konfiguraatio ja muuttujat
static PIN_Handle buttonHandle;
static PIN_State buttonState;

//Äänentoiston buzzer
static PIN_Handle buzzerHandle;
static PIN_State buzzerState;

//static PIN_Handle ledHandle; 
static PIN_Handle ledHandle0;
static PIN_Handle ledHandle1;
static PIN_State ledState;

//static PIN_Handle hPin = NULL;

static PIN_Handle hButtonShut;
static PIN_State bStateShut;

// *******************************
//
// MPU9250 I2C CONFIG
//
// *******************************
static const I2CCC26XX_I2CPinCfg i2cMPUCfg = {
    .pinSDA = Board_I2C0_SDA1,
    .pinSCL = Board_I2C0_SCL1
};

//painonappien ja ledien konfiguraatio 
PIN_Config buttonConfig[] = {
   Board_BUTTON0  | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,
   PIN_TERMINATE // Määritys lopetetaan aina tähän vakioon
};

PIN_Config ledConfig0[] = {
   Board_LED0 | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
   PIN_TERMINATE
};


PIN_Config ledConfig1[] = {
   Board_LED1 | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
   PIN_TERMINATE
};


PIN_Config buttonShut[] = {
   Board_BUTTON1 | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,
   PIN_TERMINATE
};
PIN_Config buttonWake[] = {
   Board_BUTTON1 | PIN_INPUT_EN | PIN_PULLUP | PINCC26XX_WAKEUP_NEGEDGE,
   PIN_TERMINATE
};

//Buzzerin konfiguraatio

PIN_Config buzzerConfig[] = {
    Board_BUZZER | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
    PIN_TERMINATE
};

//Painonappien task
void buttonFxn(PIN_Handle handle, PIN_Id pinId) {
    
    
    myState=READ_SENSOR;


   // Vaihdetaan led-pinnin tilaa negaatiolla
   //PIN_setOutputValue( ledHandle, Board_LED0, !PIN_getOutputValue( Board_LED0 ) );
   
   //Jos valo palaa ja nappia painetaan, valo lähtee pois
    if (PIN_getOutputValue( Board_LED0 )){
   //PIN_setOutputValue( ledHandle0, Board_LED0, !PIN_getOutputValue( Board_LED0 ) );
        PIN_setOutputValue( ledHandle0, Board_LED0, !PIN_getOutputValue( Board_LED0 ) );}
    else if(PIN_getOutputValue( Board_LED1 )){
        PIN_setOutputValue( ledHandle1, Board_LED1, !PIN_getOutputValue( Board_LED1 ) );
    }
    
}
// Napinpainalluksen käsittelijäfunktio luentodioista
Void buttonShutFxn(PIN_Handle handle, PIN_Id pinId) {

   // Näyttö pois päältä
   Display_clear(hDisplay);
   Display_close(hDisplay);
   Task_sleep(100000 / Clock_tickPeriod);

   // Itse taikamenot
   PIN_close(hButtonShut);
   PINCC26XX_setWakeup(buttonWake);
   Power_shutdown(NULL,0);
}


// Tiedonsiirron taski

/* Communication Task */
Void commTaskFxn(UArg arg0, UArg arg1) {

    // Radio to receive mode

	int32_t result = StartReceive6LoWPAN();
	if(result != true) {
		System_abort("Wireless receive mode failed");
	}

    while (1) {

        // If true, we have a message
    	if (GetRXFlag() == true) {
    	// if (GetRXFlag() == true && myState == WAIT) {
    	    //int32_t result = StartReceive6LoWPAN();
            memset(payload,0,16);
            // Luetaan viesti puskuriin payload
            Receive6LoWPAN(&senderAddr, payload, 16);
            // Tulostetaan vastaanotettu viesti konsoli-ikkunaan
if (myState == WAIT){
            System_printf(payload);
            System_flush();}


    		// Handle the received message..
        }

    	// Absolutely NO Task_sleep in this task!!
    }
}

Void labTaskFxn(UArg arg0, UArg arg1) {

    I2C_Handle i2cMPU;
	I2C_Params i2cMPUParams;
	float ax, ay, az, gx, gy, gz;
	
//	char str[80];

    
    //I2C mpu käyttöön

    
    I2C_Params_init(&i2cMPUParams);
    i2cMPUParams.bitRate = I2C_400kHz;
    i2cMPUParams.custom = (uintptr_t)&i2cMPUCfg;
    
    // MPU OPEN I2CMPU
    i2cMPU = I2C_open(Board_I2C, &i2cMPUParams);
    if (i2cMPU == NULL) {
        System_abort("Error Initializing I2CMPU\n");
    }
    else {
        System_printf("I2CMPU Initialized!\n");
    }
    
    //mpu9250 virtojen kytkeminen
    PIN_setOutputValue(hMpuPin,Board_MPU_POWER, Board_MPU_POWER_ON);
    Task_sleep(100000 / Clock_tickPeriod);
    System_printf("MPU9250: Power ON\n");
    System_flush();
        
    // MPU9250 kalibrointi

	System_printf("MPU9250: Setup and calibration...\n");
	System_flush();

	mpu9250_setup(&i2cMPU);

	System_printf("MPU9250: Setup and calibration OK\n");
	System_flush();


    


    
//Näyttö
    
    
    Display_Params displayParams;
	displayParams.lineClearMode = DISPLAY_CLEAR_BOTH;
    Display_Params_init(&displayParams);

    hDisplay = Display_open(Display_Type_LCD, &displayParams);
    if (hDisplay == NULL) {
        System_abort("Error initializing Display\n");
    }
    Display_clear(hDisplay);
    //Display_clear(hDisplay);
    Display_print0(hDisplay, 0, 11, "LIIKE");
    Display_print0(hDisplay, 11, 9, "SAMMUTA");
    


while (1) {


    //Määritellään looppien määrä i:n avulla
    float i=0;
switch (myState) {
    case READ_SENSOR:
        Display_clear(hDisplay);
        //Informoidaan käyttäjää laitteen tunnistuksen tilasta
        Display_print0(hDisplay, 5, 1, "TUNNISTETAAN");
        for(i=0; i < 61; i++){
        mpu9250_get_data(&i2cMPU, &ax, &ay, &az, &gx, &gy, &gz); 
        //char arvo[80];
        //sprintf(arvo, "%f,%f,%f,%f,%f,%f,%f\n", ax,ay,az,gx,gy,gz,i);
        //System_printf(arvo);
    	//System_flush();

    	
    	
    	//Rytmimunan algoritmi
    	if(ax > 2){
    	    if(gz < -50){
    	        if(ay > 2){
                    myState = RYTMIMUNA;
                    break;
            	    
    	        
    	    }
    	    }
    	}
    	
    	//Läpylyönnin algoritmi
    	
    	else if(az > 3){
    	    if(gy > 230) {
    	        if(gx < -110){
    	            //if(ax > -0,5){
                        myState = LAPYT;
                        break;
                        
    	            //}
    	        }
    	   }
    	}
    	else if(i > 59){
    	    myState = TUNNISTAMATON;
    	    break;
    	    
    	}
    	//Käsi vasemmalle algoritmi
    	else if(az < 0.5){ //Muutettiin else if
    	    if(gz < -100){
    	        if(gx < 0){
    	            if(ay < 0.6){
        	            if(ax > 0.5){
                            myState = VASEN;
                            break;
    	            } 
    	            }
    	        }
    	    }
    	    
    	    
    	}
	

	Task_sleep(50000 / Clock_tickPeriod);
	
    }
    break;




		
    case RYTMIMUNA:
        //kannustetaan käyttäjän onnistumista vihreällä valolla
        PIN_setOutputValue( ledHandle0, Board_LED0, !PIN_getOutputValue( Board_LED0 ) );
        System_printf("Rytmimuna\n");
        System_flush();
        Display_clear(hDisplay);
        Display_print0(hDisplay, 5, 4, "RYTMIMUNA");
        
        //Iloitaan onnistunutta liikettä iloisella musiikilla
        buzzerOpen(buzzerHandle);
        buzzerSetFrequency(392);
        Task_sleep(200000 / Clock_tickPeriod);
        buzzerSetFrequency(352);
        Task_sleep(400000 / Clock_tickPeriod);
        buzzerSetFrequency(392);
        Task_sleep(300000 / Clock_tickPeriod);
        buzzerSetFrequency(523);
        Task_sleep(500000 / Clock_tickPeriod);
        buzzerClose();
        
        Task_sleep(3000000 / Clock_tickPeriod);
        
        Display_clear(hDisplay);
        System_printf(payload);
        System_printf("\n");
        System_flush();
        Display_clear(hDisplay);
        Display_print0(hDisplay, 4, 0, "Saatu viesti");
        Display_print0(hDisplay, 6, 0, payload);
        
        Display_print0(hDisplay, 0, 11, "LIIKE");
        Display_print0(hDisplay, 11, 9, "SAMMUTA");
        
        Send6LoWPAN(0xFFFF, "Rytmimuna", 10);
        StartReceive6LoWPAN();
        
        
        // Asetetaan tila takaisin IDLE
        myState = WAIT;
        break;

    case LAPYT:

        //kannustetaan käyttäjän onnistumista vihreällä valolla
        PIN_setOutputValue( ledHandle0, Board_LED0, !PIN_getOutputValue( Board_LED0 ) );
        System_printf("Läpy\n");
	    System_flush();
        Display_clear(hDisplay);
        Display_print0(hDisplay, 5, 6, "FEMMAT");
        //Iloitaan onnistunutta liikettä iloisella musiikilla
        buzzerOpen(buzzerHandle);
        buzzerSetFrequency(329);
        Task_sleep(200000 / Clock_tickPeriod);
        buzzerSetFrequency(349);
        Task_sleep(400000 / Clock_tickPeriod);
        buzzerSetFrequency(392);
        Task_sleep(300000 / Clock_tickPeriod);
        buzzerSetFrequency(523);
        Task_sleep(500000 / Clock_tickPeriod);
        buzzerClose();
        
        Task_sleep(3000000 / Clock_tickPeriod);
        
        Display_clear(hDisplay);
        System_printf(payload);
        System_printf("\n");
        System_flush();
        Display_clear(hDisplay); 
        Display_print0(hDisplay, 4, 0, "Saatu viesti");
        Display_print0(hDisplay, 6, 0, payload);


        Display_print0(hDisplay, 0, 11, "LIIKE");
        Display_print0(hDisplay, 11, 9, "SAMMUTA");
        
        Send6LoWPAN(0xFFFF, "FEMMAT", 7);
        StartReceive6LoWPAN();


        // Asetetaan tila takaisin WAIT
        myState = WAIT;				
        break;
        
    case VASEN:
        //kannustetaan käyttäjän onnistumista vihreällä valolla
        PIN_setOutputValue( ledHandle0, Board_LED0, !PIN_getOutputValue( Board_LED0 ) );
        System_printf("Vasen\n");
        System_flush();
        Display_clear(hDisplay);
        Display_print0(hDisplay, 5, 4, "VASEN");
        //Iloitaan onnistunutta liikettä iloisella musiikilla
        buzzerOpen(buzzerHandle);
        buzzerSetFrequency(261);
        Task_sleep(400000 / Clock_tickPeriod);
        buzzerSetFrequency(329);
        Task_sleep(400000 / Clock_tickPeriod);
        buzzerSetFrequency(392);
        Task_sleep(400000 / Clock_tickPeriod);
        buzzerSetFrequency(523);
        Task_sleep(600000 / Clock_tickPeriod);
        buzzerClose();
        
        Task_sleep(3000000 / Clock_tickPeriod);
        
        Display_clear(hDisplay);
        System_printf(payload);
        System_printf("\n");
        System_flush();
        Display_clear(hDisplay);
        Display_print0(hDisplay, 4, 0, "Saatu viesti");
        Display_print0(hDisplay, 6, 0, payload);
        Display_print0(hDisplay, 0, 11, "LIIKE");
        Display_print0(hDisplay, 11, 9, "SAMMUTA");
        Send6LoWPAN(0xFFFF, "Vasen", 6);
        StartReceive6LoWPAN();
        // Asetetaan tila takaisin IDLE
        myState = WAIT;
        break;
        
    case TUNNISTAMATON:
        //rangaistaan käyttäjää punaisella ledillä epäonnistuneesta liikkeestä
        PIN_setOutputValue( ledHandle1, Board_LED1, !PIN_getOutputValue( Board_LED1 ) );
        
        System_printf("Liikettä ei tunnistettu\n");
	    System_flush();

        Send6LoWPAN(0xFFFF, "Tunnistamaton", 16);
        StartReceive6LoWPAN();
        if (hDisplay) {
            Display_clear(hDisplay);
            tContext *pContext = DisplayExt_getGrlibContext(hDisplay);
            if (pContext) {
                // Piirretään puskuriin kaksi linjaa näytön poikki x:n muotoon
                GrLineDraw(pContext,0,0,96,96); //Vasen ylä -> oikea ala
                GrLineDraw(pContext,0,96,96,0); //Oikea ylä -> vasen ala
                GrFlush(pContext);
                
                //Rangaistaan myös musiikilla
                buzzerOpen(buzzerHandle);
                buzzerSetFrequency(440);
                Task_sleep(400000 / Clock_tickPeriod);
                buzzerSetFrequency(415);
                Task_sleep(400000 / Clock_tickPeriod);
                buzzerSetFrequency(392);
                Task_sleep(400000 / Clock_tickPeriod);
                buzzerSetFrequency(369);
                Task_sleep(600000 / Clock_tickPeriod);
                buzzerClose();
                
                //Task_sleep(2000000 / Clock_tickPeriod);
                Display_clear(hDisplay);
                System_printf(payload);
                System_printf("\n");
                System_flush();
                Display_clear(hDisplay); //nää kaks rivii myös lisätty
                Display_print0(hDisplay, 4, 0, "Saatu viesti");
                Display_print0(hDisplay, 6, 0, payload);
                //Task_sleep(2000000 / Clock_tickPeriod);
                Display_print0(hDisplay, 0, 11, "LIIKE");
                Display_print0(hDisplay, 11, 9, "SAMMUTA");
                
            }
            
          }
            myState = WAIT;
            break;

    }
}

	// MPU9250 POWER OFF 
    //Loopin takia ei koskaan mene tänne
//    PIN_setOutputValue(hMpuPin,Board_MPU_POWER, Board_MPU_POWER_OFF);
}


// JTKJ: Laadi painonapille keskeytyksen k�sittelij�funktio ja toteudu sille vaadittu toiminnallisuus

Int main(void) {

    // Task variables
	Task_Handle labTask;
	Task_Params labTaskParams;
	Task_Handle commTask;
	Task_Params commTaskParams;

    // Initialize board
    Board_initGeneral();

    // JTKJ: Painonappi- ja ledipinnit k�ytt��n t�ss�

    // JTKJ: Rekister�i painonapille keskeytyksen k�sittelij�funktio
    buttonHandle = PIN_open(&buttonState, buttonConfig);
    if(!buttonHandle) {
      System_abort("Error initializing button pins\n");
    }
    
    ledHandle0 = PIN_open(&ledState, ledConfig0);
    if(!ledHandle0) {
      System_abort("Error initializing LED pins\n");
    }
    
    ledHandle1 = PIN_open(&ledState, ledConfig1);
    if(!ledHandle1) {
      System_abort("Error initializing LED pins\n");
    }

    if (PIN_registerIntCb(buttonHandle, &buttonFxn) != 0) {
      System_abort("Error registering button callback function");
    }
    
    hButtonShut = PIN_open(&bStateShut, buttonShut);
    if( !hButtonShut ) {
      System_abort("Error initializing button shut pins\n");
    }
    if (PIN_registerIntCb(hButtonShut, &buttonShutFxn) != 0) {
      System_abort("Error registering button callback function");
    }
    
    buzzerHandle = PIN_open(&buzzerState, buzzerConfig);
    if(!buzzerHandle) {
      System_abort("Error initializing buzzer pin\n");
    }
    
    
    
    //

    /* Task */
    Task_Params_init(&labTaskParams);
    labTaskParams.stackSize = STACKSIZE;
    labTaskParams.stack = &labTaskStack;
    labTaskParams.priority=2;

    labTask = Task_create(labTaskFxn, &labTaskParams, NULL);
    if (labTask == NULL) {
    	System_abort("Task create failed!");
    }

    /* Communication Task */
    Init6LoWPAN(); // This function call before use!

    Task_Params_init(&commTaskParams);
    commTaskParams.stackSize = STACKSIZE;
    commTaskParams.stack = &commTaskStack;
    commTaskParams.priority=1;

    commTask = Task_create(commTaskFxn, &commTaskParams, NULL);
    if (commTask == NULL) {
    	System_abort("Task create failed!");
    }
    
    
    System_printf("Hello world!\n");
    System_flush();
    
    
    
    /* Start BIOS */
    BIOS_start();

    return (0);
}

