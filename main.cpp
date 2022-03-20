  
// Necessary libraries and definitions
#include "mbed.h"  
 
#define max7219_reg_noop         0x00
#define max7219_reg_digit0       0x01
#define max7219_reg_digit1       0x02
#define max7219_reg_digit2       0x03
#define max7219_reg_digit3       0x04
#define max7219_reg_digit4       0x05
#define max7219_reg_digit5       0x06
#define max7219_reg_digit6       0x07
#define max7219_reg_digit7       0x08
#define max7219_reg_decodeMode   0x09
#define max7219_reg_intensity    0x0a
#define max7219_reg_scanLimit    0x0b
#define max7219_reg_shutdown     0x0c
#define max7219_reg_displayTest  0x0f

#define LOW 0
#define HIGH 1  
  

    
// VARIABLES 

// Ticker to signal a sampling interrupt 
Ticker interruptTicker;   
 
// Display 
DigitalOut load(PTD0);  
SPI max72_spi(PTD2, NC, PTD1); 
  
// Tera Term 
Serial pc(USBTX, USBRX);
      
  
// ADC and DAC of freedom board
AnalogIn analogIn(PTB0);  
AnalogOut analogOut(PTE30);     

// Timers   
Timer BPMTimer;

 
// Necessary variables
bool thereIsPrevious = false , initialThreshold = true , traceOrBPM = true , higher = true , lower = false , firstBPM = true;
unsigned int value , previousValue , filteredValue , range , thresholdHigher , thresholdLower , valueToDisplay;  
unsigned int nonProcessedValues[200], BPMTimes[5];  
float alpha = 0.3;
int nonProcessedSamples , nonProcessedPointer, every5samples , BPMTimerPointer; 
   
    
// Numbers for BPM display
char zero[] = {0x00 , 0x7c , 0x44, 0x7c};
char one[] = {0x00,0x44,0x7c,0x40}; 
char two[] = {0x00,0x74,0x54,0x5c}; 
char three[] = {0x00 , 0x54,0x54 , 0x7c}; 
char four[] = {0x00 , 0x1c , 0x10 , 0x7c}; 
char five[] = {0x00 , 0x5c , 0x54 , 0x74}; 
char six[] = {0x00 , 0x7c , 0x54 , 0x74}; 
char seven[] = {0x00 , 0x04 , 0x04 , 0x7c };   
char eight[] = {0x00 , 0x7c , 0x54 , 0x7c }; 
char nine[] = {0x00 , 0x1c , 0x14 , 0x7c};  
char numbers[10][4] = {{0x00 , 0x7c , 0x44, 0x7c} ,{0x00,0x44,0x7c,0x40}, {0x00,0x74,0x54,0x5c} , {0x00 , 0x54,0x54 , 0x7c}, {0x00 , 0x1c , 0x10 , 0x7c} , {0x00 , 0x5c , 0x54 , 0x74} , {0x00 , 0x7c , 0x54 , 0x74} , {0x00 , 0x04 , 0x04 , 0x7c } , {0x00 , 0x7c , 0x54 , 0x7c } , {0x00 , 0x1c , 0x14 , 0x7c}};

// Storing each column of the trace
char storedTrace[8] = {0x00 , 0x00 , 0x00 , 0x00 , 0x00 , 0x00, 0x00 , 0x00}; 
 
  

   

// Display Functions - provided by Lecturer
void write_to_max( int reg, int col)
{ 
    load = LOW;            // begin
    max72_spi.write(reg);  // specify register
    max72_spi.write(col);  // put data
    load = HIGH;           // make sure data is loaded (on rising edge of LOAD/CS)
}
 
 void clear(){
     for (int e=1; e<=8; e++) {    // empty registers, turn all LEDs off
        write_to_max(e,0);
    }
}   
 
void pattern_to_display(char *testdata){ 
    int cdata; 
    for(int idx = 0; idx <= 7; idx++) {
        cdata = testdata[idx]; 
        write_to_max(idx+1,cdata);
    }
} 
 
void setup_dot_matrix ()
{
    // initiation of the max 7219
    // SPI setup: 8 bits, mode 0
    max72_spi.format(8, 0);
     
  
  
       max72_spi.frequency(100000); //down to 100khx easier to scope ;-)
      

    write_to_max(max7219_reg_scanLimit, 0x07);
    write_to_max(max7219_reg_decodeMode, 0x00);  // using an led matrix (not digits)
    write_to_max(max7219_reg_shutdown, 0x01);    // not in shutdown mode
    write_to_max(max7219_reg_displayTest, 0x00); // no display test
    for (int e=1; e<=8; e++) {    // empty registers, turn all LEDs off
        write_to_max(e,0);
    }
   // maxAll(max7219_reg_intensity, 0x0f & 0x0f);    // the first 0x0f is the value you can set
     write_to_max(max7219_reg_intensity,  0x08);     
 
} 
 
    
     

// Filtering Helper Functions
unsigned int filterRemainingNoise() { 
        unsigned int result = float(alpha * value + (1-alpha) * previousValue);  
        return result;
    }   
       
        
// Removal of background trend
unsigned int backgroundTrendCalc(int n) {    
        unsigned int sum = 0;
        for (int i = 0; i < n; i++) {  
            sum += nonProcessedValues[i];  
        }
        return (sum / n); 
    }    
     
    
     

// Threshold finding helper function     

   void findThreshold() {      
       range = 65535 / 8; // 16 bit range   
       thresholdLower =  (3 * range);    // 30%
       thresholdHigher = (6 * range);      // 60%                                                                                                                                    

   }
       
// Function running on each interrupt - interrupt routine
void sampler() { 
      
    // reading value
    value = analogIn.read_u16();  
     
    // if it is the first value - set previous to itself, nullyfying filtering equation
    if (!thereIsPrevious) { 
        previousValue = value; 
        thereIsPrevious = true;
    } 
       
    // update number of samples
    nonProcessedSamples = nonProcessedSamples + 1; 
    if (nonProcessedSamples > 200) { 
        nonProcessedSamples = 201;
    } 


      
    // Filter equation
    filteredValue = filterRemainingNoise();  
    previousValue = value;

 
  
    // Storing filtered sample
    nonProcessedValues[nonProcessedPointer] = value;
    nonProcessedPointer = (nonProcessedPointer+1) % 200;  
    value = filteredValue;  
     
    // Once we have enough samples 
    if (nonProcessedSamples > 200) { 
          
        // Removing background trend 
        // Gave weird results,so was not used in final code
        unsigned int backgroundTrend = backgroundTrendCalc(200);   
       // value = value - backgroundTrend; 
           
        // getting the threshold
        if (initialThreshold) {
            findThreshold();    
            initialThreshold = false;   
             
            // starting the timer for BPM
            BPMTimer.start();
        } 
           
        // The line below would be used if background trend removal was used
       // value += (60000 / 2); 
 
        // update the amount of samples between last time trace was updated
        every5samples = (every5samples + 1) % 5;  
         
        switch (traceOrBPM) {  
            case true:     
                // trace case
                if (every5samples == 4) { 
                    clear(); 
                    char charToDisplay;
                    int valueToDisplay = (((value) * 8) / 65536);    // convert to 8 bits 
                    pc.printf("Display %d \n\r" , valueToDisplay); 
                     
                    // new value to display
                    switch (valueToDisplay) { 
                        case 1: 
                            charToDisplay = 0x01; 
                            break; 
                        case 2: 
                            charToDisplay = 0x02; 
                            break;  
                        case 3: 
                            charToDisplay = 0x04; 
                            break;   
                        case 4: 
                            charToDisplay = 0x08; 
                            break;  
                        case 5: 
                            charToDisplay = 0x10; 
                            break;  
                        case 6: 
                            charToDisplay = 0x20; 
                            break;  
                        case 7: 
                            charToDisplay = 0x40; 
                            break; 
                        case 8: 
                            charToDisplay = 0x80; 
                            break;  
                        default: 
                            break; 
                        } 

                    // shifting each display column     
                         
                    for (int i= 1 ; i < 8 ; i++) {   
                        storedTrace[i-1] = storedTrace[i]; 
                    }  
    
                    storedTrace[7] = charToDisplay;  
                     
                     
                    // display
                    
                    pattern_to_display(storedTrace);     
                              
                }
                break; 
            case false:  
                // do BPM 
                 
                 // if we are looking for higher 
                if (value > thresholdHigher && higher && !firstBPM) {   
                     
                    // calculate BPM
                    unsigned int rate = (1 / BPMTimer.read()) * 60; 
                        
                    // update amount of bpm samples
                    BPMTimerPointer = ((BPMTimerPointer + 1) % 5);  
                     
                    // store
                    BPMTimes[BPMTimerPointer] = rate;   
                     
                    // if we have all samples, work out average
                    if (BPMTimerPointer == 4) { 
                        unsigned int sum = 0; 
                        for (int i = 0 ; i < 5 ; i++) { 
                            sum += BPMTimes[i]; 
                    }  
                     
                    // work out units, tens, hundreds based on average
                    rate = (sum / 5);     
                    unsigned int units = rate % 10;
                    unsigned int tens = (rate /10) % 10; 
                    unsigned int hundreds = (rate/100) % 10;  
                    char result[8];  
                         
                    // combine units  + tens
                    for(int idx = 0; idx <= 3; idx++) {
                        result[idx] = numbers[tens][idx]; 
                        result[idx+4] = numbers[units][idx];
                    }   
                     
                    // we run out of time to display the hundreds,as our second display was not fully working

                    pc.printf("Rate %d \n\r" , rate); 
                    pc.printf("Units %d \n\r" , units); 
                    pc.printf("Tens %d \n\r" , tens);
                    pattern_to_display(result); 
                     
                        // number tests
                        //while (true) {  
//                        //pattern_to_display(result); 
////                        wait(0.5);
//                        pattern_to_display(zero); 
//                        wait(0.5);
//                        pattern_to_display(one); 
//                        wait(0.5); 
//                        pattern_to_display(two); 
//                        wait(0.5); 
//                        pattern_to_display(three); 
//                        wait(0.5);  
//                        pattern_to_display(four); 
//                        wait(0.5);  
//                        pattern_to_display(five); 
//                        wait(0.5);  
//                        pattern_to_display(six); 
//                        wait(0.5);  
//                        pattern_to_display(seven); 
//                        wait(0.5);  
//                        pattern_to_display(eight); 
//                        wait(0.5);  
//                        } 
                        
                        
                       //
//                        switch (rate) { 
//                            case}
                        
                    }
                    // update so that we are looking for the lower threshold on the next sample
                    lower = !lower;  
                    higher = !higher; 
                      
                                    
                    
                } else if (value < thresholdLower && lower) {    
                     
                        // looking for higher and restarting timer
                        higher = !higher; 
                        lower = !lower; 
                        BPMTimer.reset();
                        BPMTimer.start(); 

                } 
                break;
        } 
        


         
    
         
        

    } 
     


}
     







int main() {    
        // message to show on startup  
        pc.printf("Startting up \n\r"); 
         
        // ensuring the display is ready and empty
        setup_dot_matrix ();  
        clear();   
        wait(0.2);  

        // sampling the signal at 200Hz i.e. a sample every 0.005s  
        interruptTicker.attach(&sampler, 0.005);    

        // code to run indefinitely 
        while (true) { } 
}
 

