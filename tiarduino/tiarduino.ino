/* TI-99 Keyboard to USB
   (C) 2020 Manolis Kiagias
   For Schematics, please check:
   https://github.com/sonic2000gr/ti-keyboard */

#include "Keyboard.h"

// Rows and columns of the keyboard
// For TI-99 we have 6 normal rows
// and 8 columns. There is also alpha lock
// on a row pin of it's own, handled elsewhere

#define ROWS 6
#define COLS 8

// Indexes of modifier keys on last row
// Also used as return values

#define SHIFT 5
#define CTRL 6
#define FCTN 7

// Keyboard delay is set for debouncing
// and setting keyboard repeat rate

#define KBD_DELAY 150
#define RPT_DELAY 50
#define DEBUG

// This pin must be high for the keyboard function to be enabled
// This acts as a safety mode for problems that may make the 
// keyboard run amok just typing nonsense on the USB :D
// You don't need to make a physical connection to this
// pin, by default it is pulled HIGH and thus enabled.

const int enablePin = 13;

// This defines the Pin where Alpha Lock is connected
// Alpha Lock has it's own dedicated row on the TI
// Connector, at pin 6

const int alphaLockRowPin = 7;

// The char array contains all the normal chars for
// each row and column
// /0 indicate modifier / not existing keys
// (there are two empty keys)

char keys[ROWS][COLS] = { {'/', ';', 'p', '0', 'z', 'a', 'q', '1'},
                          {'.', 'l', 'o', '9', 'x', 's', 'w', '2'},                          
                          {',', 'k', 'i', '8', 'c', 'd', 'e', '3'},
                          {'m', 'j', 'u', '7', 'v', 'f', 'r', '4'},
                          {'n', 'h', 'y', '6', 'b', 'g', 't', '5'},
                          {'=', ' ', '\n', '\0', '\0', '\0', '\0', '\0'}  
                        };

// How many columns to scan per row. The last row is not scanned
// to the last column, modifiers are handled elsewhere

int columns[]={8, 8, 8, 8, 8, 3};

// rowPins contains the arduino pins connected to each row
// This is set so the TI-99 connector goes more or less flat in
// the arduino micro port (see schematic)

int rowPins[ROWS] = {9, A1, A2, A3, 10, A0};


// colPins contains the arduino pins connected to each column
// See note on rowPins above

int colPins[COLS] = {6, 5, 2, 3, 12, 4, 11, 8};

// We need to globally keep state of these
// when loop function restarts, hence
// declared here

int previousKey = 0;
int currentKey = 0;
int count = 0;
unsigned long idlecount = 0;

// Setup all the column pins
// as inputs with a pullup resistor
// They will be kept HIGH until a key
// is pressed on the row currently scanned

void setup_pins() {
  
  // Set the enablePin to enable the keyboard by default
  
  pinMode(enablePin, INPUT_PULLUP);
  
  // Setup columns and rows
  
  for (int i = 0; i < COLS; i++)
    pinMode(colPins[i], INPUT_PULLUP);
  
  for (int i = 0; i < ROWS; i++) 
    pinMode(rowPins[i], INPUT);
  
  // Finally setup the initial state of the the alpha lock
  
  pinMode(alphaLockRowPin, INPUT_PULLUP);
  
}

// Scan the current row by setting it to OUTPUT / LOW
// after setting the previously active row to INPUT

void scan_row(int previousRow, int currentRow) {
  pinMode(rowPins[previousRow], INPUT);
  pinMode(rowPins[currentRow], OUTPUT);
  digitalWrite(rowPins[currentRow], LOW);  
}

void disable_lastrow() {
  pinMode(rowPins[ROWS-1], INPUT);
}

// Check whether Alpha Lock is on or off
// On the TI this is a real switch (!)
// assigned to it's own row.
// First check Alpha Lock (Caps Lock) state
// This is checked once per complete keyboard scan
// Caps Lock is special: You have to press/release
// immediately and the value is latched.
// (Hence the 'Lock' and the requirement to keep state)
// Normally the host can set and release the Lock
// independently of the keyboard action (and notify
// of the state any connected keyboards).
// However there is no easy way to read the previous
// alpha lock host state in arduino.
// Hence, you may find typing small letters when 
// TI alpha lock is 'On' and CAPS when it's 'Off'
// Just a small annoyance...
// Depending on your system / emulator settings
// you may need to start with Alpha Lock on or off
// for this to work properly.
// For example in my MAME settings, TI defaults to
// Alpha Lock on at startup, so I keep Alpha Lock on
// initially.

void checkAlphaLock() {
  static int alphaLock = 0;
  
  // Last row active before alpha lock gets scanned is the last
  // one. Therefore we disable it as well (no need to reenable either)
  // Alpha Lock must be read after modifiers are read!

  disable_lastrow();
  pinMode(alphaLockRowPin, OUTPUT);
  digitalWrite(alphaLockRowPin, LOW);
  int alphaLockOff = digitalRead(colPins[7]);
  pinMode(alphaLockRowPin, INPUT_PULLUP);
  if (!alphaLockOff && !alphaLock) {
      alphaLock = 1;
      Keyboard.press(KEY_CAPS_LOCK);
      Keyboard.release(KEY_CAPS_LOCK);
      #ifdef DEBUG
      Serial.println("alphaLock on");
      #endif
    } else if (alphaLockOff && alphaLock) {
      alphaLock = 0;
      Keyboard.press(KEY_CAPS_LOCK);
      Keyboard.release(KEY_CAPS_LOCK);
      #ifdef DEBUG
      Serial.println("alphaLock off");
      #endif
    }
}

// Check whether a modifier key is pressed
// There is Shift, Fctn and Ctrl on the TI Keyboard
// For emulation purpose, TI Fctn = PC Alt,
// Ctrl and Shift map to Ctrl and Shift as usual
// TI does not process more than one modifier key
// at any one time. If you wish to do this change the code
// here and on the loop to allow it. This will make
// the keyboard respond more like a traditional
// PC keyboard. With all the keys missing I wonder
// who would want it on an actual PC though! 

int modifierPressed() {
  if (!digitalRead(colPins[SHIFT]))
    return SHIFT;
  if (!digitalRead(colPins[CTRL]))
    return CTRL;
  if (!digitalRead(colPins[FCTN]))
    return FCTN;
  return 0;
}

// Send the modifier key that was detected
// before, use this function just before
// sending the character

void pressModifier(int modifier) {
  if (modifier == SHIFT)
    Keyboard.press(KEY_LEFT_SHIFT);
  else if (modifier == CTRL)
    Keyboard.press(KEY_LEFT_CTRL);
  else if (modifier == FCTN)
    Keyboard.press(KEY_LEFT_ALT);
}

// Perform all necessary initializations

void setup() {
  
  // Setup all input pins (columns and enablePin)
  // Columns are initialized with an input pullup
  // and are pulled down when a key is pressed
  // Setup rows as inputs too, these alternate to
  // outputs when scanned
  
  setup_pins();
    
  // initialize keyboard HID

  // Activate last row for the first run
  
  pinMode(rowPins[ROWS-1], OUTPUT);
  digitalWrite(rowPins[ROWS-1], LOW);
  
  Keyboard.begin();
  #ifdef DEBUG
    Serial.begin(9600);
  #endif
}

void loop() {
  
  // i Represents currentRow
  // j Represents currentColumn
  // These are so common in loops and I hate typing
  // long variables as arrays indices :D
  
  int i,j, previousRow, modifier;

  // Get the state of the enablePin
  // Keyboard read / output will be disabled
  // if this is not HIGH
  
  int enableState = digitalRead(enablePin);
  
  // Last row scanned during loop will be the last one
  // We need to make it high again while row 0 is scanned
  
  previousRow = ROWS - 1;

  // Enter the loop if keyboard is enabled
  
  if (enableState == HIGH) {
    modifier = modifierPressed();
    checkAlphaLock();
 
    // Now scan the rows

    for ( i = 0; i < ROWS; i++) {
       
      // Scan the current row after pulling HIGH the previous one

      scan_row(previousRow, i);
      
      // Next time's previousRow will be this time's currentRow

      previousRow = i;

      // Now scan the columns for any key pressed in the
      // currentRow
      // Last column we do not scan for modifier keys,
      // these are handled separately
      
      for (j = 0; j < columns[i]; j++) {      

        if (!digitalRead(colPins[j])) {

            // Send the corresponding key code rather than
            // the actual keyboard character (use press instead of print)

            currentKey = keys[i][j];

            if ((currentKey != previousKey) || (idlecount >= 10000 )) {
              if (modifier)
                pressModifier(modifier);
              Keyboard.press(currentKey);
              delay(KBD_DELAY);
              count = 0;
              idlecount = 0;
              #ifdef DEBUG
                Serial.println("Different key or idlecount timeout");
              #endif
            } else {
              count++;
              if (count > 150) { 
                if (modifier)
                  pressModifier(modifier);
                Keyboard.press(currentKey);
                delay(RPT_DELAY);
                idlecount = 0;
                #ifdef DEBUG
                  Serial.println("Key repeat");
                #endif
              }
            }
            Keyboard.releaseAll();            
            previousKey = currentKey;
        } else {
          idlecount ++;
        }
      }       
    }
  }
}
