/*
  =====================================================
  OTP Voting Machine — RTC ONLY MODE (No GSM)
  =====================================================
  Use this code when GSM module is NOT working.
  OTP is generated using only the DS3231 RTC module.

  HOW IT WORKS:
  - Voter selects candidate (1-4)
  - OTP is generated from RTC time + secret key
  - OTP is shown on LCD for 5 seconds
  - Voter enters OTP using keypad
  - Vote is confirmed and saved to SD card

  FIXED PIN MAPPING:
  - Buzzer  → A1  (NOT D1 — D1 is Serial TX!)
  - Green LED → A2  (NOT D0 — D0 is Serial RX!)
  - Red LED   → A0

  NO GSM MODULE NEEDED FOR THIS CODE.
  =====================================================
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include <Keypad.h>
#include <SPI.h>
#include <SD.h>

// ---- Pin Definitions ----
#define BUZZER_PIN  A1    // Fixed: was D1
#define LED_GREEN   A2    // Fixed: was D0
#define LED_RED     A0
#define SD_CS       10

// ---- LCD (I2C address 0x27 or 0x3F) ----
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ---- RTC ----
RTC_DS3231 rtc;

// ---- Keypad (4x4) ----
const byte ROWS = 4, COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {5, 6, 7, 8};
byte colPins[COLS] = {2, 3, 4, 9};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ---- Secret Key for OTP ----
// Change this number to make your OTP unique
const uint32_t SECRET_KEY = 987654UL;

// ---- OTP Time Window ----
// OTP changes every 30 seconds
#define OTP_WINDOW 30

// ---- How long OTP is shown on LCD (seconds) ----
#define OTP_DISPLAY_TIME 5000  // 5 seconds

// ---- Max wrong attempts before lockout ----
#define MAX_ATTEMPTS 3

// ---- Voting State ----
int  candidateSelected = 0;
bool waitingForOTP     = false;
String enteredOTP      = "";
int  wrongAttempts     = 0;
bool machineLocked     = false;

// =====================================================
// Generate OTP using RTC time only (No GSM needed)
// =====================================================
String generateOTP_RTC() {
  DateTime now = rtc.now();

  // Get current time step (changes every OTP_WINDOW seconds)
  uint32_t timeStep = now.unixtime() / OTP_WINDOW;

  // Mix time step with secret key to make OTP
  uint32_t otpRaw = (timeStep ^ SECRET_KEY) * 1000003UL;
  uint32_t otp    = otpRaw % 1000000;  // 6-digit OTP

  // Pad with leading zeros if needed (e.g. 000123)
  String otpStr = String(otp);
  while (otpStr.length() < 6) {
    otpStr = "0" + otpStr;
  }
  return otpStr;
}

// =====================================================
// Show current RTC time on LCD bottom row
// =====================================================
void showTimeOnLCD() {
  DateTime now = rtc.now();
  lcd.setCursor(0, 1);
  // Format: HH:MM:SS
  if (now.hour()   < 10) lcd.print("0");
  lcd.print(now.hour());
  lcd.print(":");
  if (now.minute() < 10) lcd.print("0");
  lcd.print(now.minute());
  lcd.print(":");
  if (now.second() < 10) lcd.print("0");
  lcd.print(now.second());
  lcd.print(" [RTC]  ");
}

// =====================================================
// Log vote to SD card with timestamp
// =====================================================
void logVoteToSD(int cand, String otp) {
  DateTime now = rtc.now();
  File f = SD.open("votes.txt", FILE_WRITE);
  if (f) {
    f.print(now.timestamp());
    f.print(" | Candidate: ");
    f.print(cand);
    f.print(" | OTP: ");
    f.print(otp);
    f.println(" | MODE: RTC-ONLY");
    f.close();
  }
}

// =====================================================
// Feedback: Vote accepted
// =====================================================
void voteAccepted() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("  VOTE CAST!  ");
  lcd.setCursor(0, 1); lcd.print("  Thank you!  ");
  digitalWrite(LED_GREEN, HIGH);
  tone(BUZZER_PIN, 1200, 200);
  delay(200);
  tone(BUZZER_PIN, 1500, 400);
  delay(2000);
  digitalWrite(LED_GREEN, LOW);
}

// =====================================================
// Feedback: Wrong OTP
// =====================================================
void wrongOTP() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("  Wrong OTP!  ");
  lcd.setCursor(0, 1);
  lcd.print("Tries left: ");
  lcd.print(MAX_ATTEMPTS - wrongAttempts);
  digitalWrite(LED_RED, HIGH);
  tone(BUZZER_PIN, 300, 800);
  delay(2000);
  digitalWrite(LED_RED, LOW);
}

// =====================================================
// Lockout: Too many wrong attempts
// =====================================================
void lockoutScreen() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(" MACHINE LOCKED ");
  lcd.setCursor(0, 1); lcd.print(" Call operator  ");
  digitalWrite(LED_RED, HIGH);
  // Fast beep 5 times
  for (int i = 0; i < 5; i++) {
    tone(BUZZER_PIN, 500, 150);
    delay(300);
  }
  // Stay locked — machine needs restart
  while (true) {
    digitalWrite(LED_RED, HIGH);
    delay(500);
    digitalWrite(LED_RED, LOW);
    delay(500);
  }
}

// =====================================================
// Welcome screen
// =====================================================
void showWelcome() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("VOTING MACHINE");
  lcd.setCursor(0, 1); lcd.print("Press 1-4 vote");
}

// =====================================================
// Setup
// =====================================================
void setup() {
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_GREEN,  OUTPUT);
  pinMode(LED_RED,    OUTPUT);

  lcd.init();
  lcd.backlight();

  // Check RTC
  if (!rtc.begin()) {
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("RTC ERROR!");
    lcd.setCursor(0, 1); lcd.print("Check wiring");
    digitalWrite(LED_RED, HIGH);
    while (1);
  }

  // Check if RTC lost power (time reset)
  if (rtc.lostPower()) {
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("RTC time lost!");
    lcd.setCursor(0, 1); lcd.print("Setting time...");
    // Set RTC to compile time
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    delay(2000);
  }

  // Check SD card
  if (!SD.begin(SD_CS)) {
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("SD ERROR!");
    lcd.setCursor(0, 1); lcd.print("No SD card?");
    digitalWrite(LED_RED, HIGH);
    delay(3000);
    digitalWrite(LED_RED, LOW);
    // Continue without SD (votes won't be saved)
  }

  // Show RTC-only mode notice
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("MODE: RTC ONLY");
  lcd.setCursor(0, 1); lcd.print("GSM not needed");
  delay(2500);

  showWelcome();
}

// =====================================================
// Main Loop
// =====================================================
void loop() {

  // Show live clock on idle screen
  if (!waitingForOTP && !machineLocked) {
    showTimeOnLCD();
  }

  char key = keypad.getKey();
  if (!key) return;

  // ── STEP 1: Select Candidate ──────────────────────
  if (!waitingForOTP) {
    if (key >= '1' && key <= '4') {
      candidateSelected = key - '0';
      wrongAttempts     = 0;

      // Generate OTP from RTC
      String otp = generateOTP_RTC();

      // Show candidate + OTP on LCD
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Candidate: ");
      lcd.print(candidateSelected);
      lcd.setCursor(0, 1);
      lcd.print("OTP: ");
      lcd.print(otp);

      // Keep OTP visible for 5 seconds
      delay(OTP_DISPLAY_TIME);

      // Now ask voter to enter OTP
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("Enter 6-digit");
      lcd.setCursor(0, 1); lcd.print("OTP: ");
      enteredOTP    = "";
      waitingForOTP = true;
    }

  // ── STEP 2: Enter OTP ────────────────────────────
  } else {

    // * key = clear entry
    if (key == '*') {
      enteredOTP = "";
      lcd.setCursor(0, 1);
      lcd.print("OTP:           ");
      lcd.setCursor(5, 1);
    }

    // # key = confirm OTP
    else if (key == '#') {
      String validOTP = generateOTP_RTC();

      if (enteredOTP == validOTP) {
        // ✅ Correct OTP
        logVoteToSD(candidateSelected, enteredOTP);
        voteAccepted();
        wrongAttempts     = 0;
        waitingForOTP     = false;
        candidateSelected = 0;
        enteredOTP        = "";
        showWelcome();

      } else {
        // ❌ Wrong OTP
        wrongAttempts++;
        if (wrongAttempts >= MAX_ATTEMPTS) {
          lockoutScreen(); // Machine locks after 3 wrong tries
        } else {
          wrongOTP();
          // Let voter try again
          lcd.clear();
          lcd.setCursor(0, 0); lcd.print("Enter OTP again:");
          lcd.setCursor(0, 1); lcd.print("OTP: ");
          enteredOTP = "";
        }
      }
    }

    // Number keys = enter OTP digits
    else if (isDigit(key) && enteredOTP.length() < 6) {
      enteredOTP += key;
      // Show * for each digit (hidden for security)
      lcd.setCursor(5 + enteredOTP.length() - 1, 1);
      lcd.print('*');
    }
  }
}
