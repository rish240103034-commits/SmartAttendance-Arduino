#include <SPI.h>
#include <MFRC522.h>
#include <LiquidCrystal.h>
#include <IRremote.hpp>

// =====================================================
// PIN CONFIGURATION
// =====================================================

// RC522 RFID
#define RFID_SS_PIN 10
#define RFID_RST_PIN 9

// IR receiver
#define IR_RECEIVE_PIN A0

// Active buzzer
#define BUZZER_PIN 8

// LCD: RS, EN, D4, D5, D6, D7
LiquidCrystal lcd(7, 6, 5, 4, 3, 2);

// RFID object
MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);

// =====================================================
// IR REMOTE COMMANDS
// =====================================================

#define BUTTON_POWER 0x12
#define BUTTON_MUTE  0x10
#define BUTTON_OK    0x0A
#define BUTTON_UP    0x19
#define BUTTON_DOWN  0x1D
#define BUTTON_RIGHT 0x47
#define BUTTON_LEFT  0x46
#define BUTTON_BACK  0x40
#define BUTTON_RESET 0x51
#define BUTTON_MENU  0x5B

// =====================================================
// STUDENT DATABASE
// Replace these UIDs with actual RFID UIDs
// =====================================================

const byte TOTAL_STUDENTS = 3;

String studentUID[TOTAL_STUDENTS] = {
  "05 C2 D5 05",
  "06 E5 97 04",
  "12 34 56 78"
};

String studentName[TOTAL_STUDENTS] = {
  "Rishabh",
  "Student 2",
  "Student 3"
};

bool studentPresent[TOTAL_STUDENTS] = {
  false,
  false,
  false
};

// =====================================================
// SYSTEM VARIABLES
// =====================================================

bool systemActive = false;
bool attendanceActive = false;
bool buzzerEnabled = true;

byte totalPresent = 0;
byte selectedStudent = 0;

unsigned long lastRFIDScan = 0;
const unsigned long RFID_SCAN_DELAY = 1500;

// =====================================================
// SCREEN DEFINITIONS
// =====================================================

enum Screen {
  SCREEN_OFF,
  SCREEN_HOME,
  SCREEN_MAIN_MENU,
  SCREEN_ATTENDANCE_MENU,
  SCREEN_SCAN_ATTENDANCE,
  SCREEN_STUDENT_RECORDS,
  SCREEN_REPORT,
  SCREEN_RESET_CONFIRM
};

Screen currentScreen = SCREEN_OFF;

// =====================================================
// MENU DATA
// =====================================================

const byte MAIN_MENU_ITEMS = 3;

const char *mainMenuItems[MAIN_MENU_ITEMS] = {
  "Attendance",
  "Student Records",
  "Report"
};

byte mainMenuIndex = 0;

const byte ATTENDANCE_MENU_ITEMS = 4;

const char *attendanceMenuItems[ATTENDANCE_MENU_ITEMS] = {
  "Start Session",
  "End Session",
  "View Count",
  "Reset Attendance"
};

byte attendanceMenuIndex = 0;

// Tracks where reset was opened from
bool resetOpenedFromAttendanceMenu = false;

// =====================================================
// FUNCTION DECLARATIONS
// =====================================================

void checkIRRemote();
void handleIRCommand(byte command);

void toggleSystemPower();
void showOffScreen();

void showHomeScreen();
void handleHomeScreen(byte command);

void showMainMenu();
void handleMainMenu(byte command);

void showAttendanceMenu();
void handleAttendanceMenu(byte command);

void startAttendanceSession();
void endAttendanceSession();
void showAttendanceScanScreen();
void handleAttendanceScreen(byte command);
void showLiveCount();

void checkRFID();
String getCardUID();
int findStudent(String scannedUID);
void markStudentPresent(byte index);
void showUnknownCard(String uid);

void showStudentRecord();
void handleStudentRecords(byte command);

void showReport();
void handleReportScreen(byte command);
void printAttendanceReport();

void showResetConfirmation();
void handleResetConfirmation(byte command);
void resetAttendance();

void toggleBuzzer();
void navigationBeep();
void successBeep();
void warningBeep();
void errorBeep();
void shutdownBeep();

void redrawCurrentScreen();
void printLCDText(String text, byte startColumn = 0);

// =====================================================
// SETUP
// =====================================================

void setup() {
  Serial.begin(9600);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  lcd.begin(16, 2);

  SPI.begin();
  rfid.PCD_Init();

  IrReceiver.begin(IR_RECEIVE_PIN, DISABLE_LED_FEEDBACK);

  showOffScreen();

  Serial.println(F("Smart Attendance System Ready"));
  Serial.println(F("Press POWER button on remote"));
}

// =====================================================
// MAIN LOOP
// =====================================================

void loop() {
  checkIRRemote();

  if (systemActive && attendanceActive) {
    checkRFID();
  }
}

// =====================================================
// IR REMOTE
// =====================================================

void checkIRRemote() {
  if (!IrReceiver.decode()) {
    return;
  }

  byte command = IrReceiver.decodedIRData.command;

  Serial.print(F("IR Command: 0x"));

  if (command < 0x10) {
    Serial.print("0");
  }

  Serial.println(command, HEX);

  bool isRepeat =
    IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT;

  if (!isRepeat) {
    handleIRCommand(command);
  }

  IrReceiver.resume();
}

void handleIRCommand(byte command) {

  // POWER works even when system is off
  if (command == BUTTON_POWER) {
    toggleSystemPower();
    return;
  }

  // Ignore all other buttons while system is off
  if (!systemActive) {
    return;
  }

  // Mute works from every active screen
  if (command == BUTTON_MUTE) {
    toggleBuzzer();
    return;
  }

  // Remote RESET button
  if (command == BUTTON_RESET) {
    resetOpenedFromAttendanceMenu = false;
    currentScreen = SCREEN_RESET_CONFIRM;
    showResetConfirmation();
    return;
  }

  // MENU opens main menu
  if (command == BUTTON_MENU) {
    currentScreen = SCREEN_MAIN_MENU;
    mainMenuIndex = 0;

    navigationBeep();
    showMainMenu();
    return;
  }

  switch (currentScreen) {

    case SCREEN_HOME:
      handleHomeScreen(command);
      break;

    case SCREEN_MAIN_MENU:
      handleMainMenu(command);
      break;

    case SCREEN_ATTENDANCE_MENU:
      handleAttendanceMenu(command);
      break;

    case SCREEN_SCAN_ATTENDANCE:
      handleAttendanceScreen(command);
      break;

    case SCREEN_STUDENT_RECORDS:
      handleStudentRecords(command);
      break;

    case SCREEN_REPORT:
      handleReportScreen(command);
      break;

    case SCREEN_RESET_CONFIRM:
      handleResetConfirmation(command);
      break;

    default:
      break;
  }
}

// =====================================================
// POWER CONTROL
// =====================================================

void toggleSystemPower() {

  if (!systemActive) {
    systemActive = true;
    currentScreen = SCREEN_HOME;

    successBeep();

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Smart Attendance"));

    lcd.setCursor(0, 1);
    lcd.print(F("Starting..."));

    delay(1200);

    showHomeScreen();

    Serial.println(F("System activated"));
  }

  else {
    systemActive = false;
    attendanceActive = false;
    currentScreen = SCREEN_OFF;

    shutdownBeep();
    showOffScreen();

    Serial.println(F("System deactivated"));
  }
}

void showOffScreen() {
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print(F("System OFF"));

  lcd.setCursor(0, 1);
  lcd.print(F("Press ON"));
}

// =====================================================
// HOME SCREEN
// =====================================================

void showHomeScreen() {
  currentScreen = SCREEN_HOME;

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print(F("System Ready"));

  lcd.setCursor(0, 1);

  if (attendanceActive) {
    lcd.print(F("Attendance ON"));
  } else {
    lcd.print(F("Press MENU"));
  }
}

void handleHomeScreen(byte command) {

  if (command == BUTTON_OK && attendanceActive) {
    currentScreen = SCREEN_SCAN_ATTENDANCE;
    showAttendanceScanScreen();
  }
}

// =====================================================
// MAIN MENU
// =====================================================

void showMainMenu() {
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print(F("Main Menu"));

  lcd.setCursor(0, 1);
  lcd.print(">");

  printLCDText(String(mainMenuItems[mainMenuIndex]), 1);
}

void handleMainMenu(byte command) {

  if (command == BUTTON_UP) {

    if (mainMenuIndex == 0) {
      mainMenuIndex = MAIN_MENU_ITEMS - 1;
    } else {
      mainMenuIndex--;
    }

    navigationBeep();
    showMainMenu();
  }

  else if (command == BUTTON_DOWN) {

    mainMenuIndex++;

    if (mainMenuIndex >= MAIN_MENU_ITEMS) {
      mainMenuIndex = 0;
    }

    navigationBeep();
    showMainMenu();
  }

  else if (command == BUTTON_OK) {

    successBeep();

    if (mainMenuIndex == 0) {
      currentScreen = SCREEN_ATTENDANCE_MENU;
      attendanceMenuIndex = 0;
      showAttendanceMenu();
    }

    else if (mainMenuIndex == 1) {
      currentScreen = SCREEN_STUDENT_RECORDS;
      selectedStudent = 0;
      showStudentRecord();
    }

    else if (mainMenuIndex == 2) {
      currentScreen = SCREEN_REPORT;
      showReport();
    }
  }

  else if (command == BUTTON_BACK) {
    navigationBeep();
    showHomeScreen();
  }
}

// =====================================================
// ATTENDANCE MENU
// =====================================================

void showAttendanceMenu() {
  currentScreen = SCREEN_ATTENDANCE_MENU;

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print(F("Attendance"));

  lcd.setCursor(0, 1);
  lcd.print(">");

  printLCDText(
    String(attendanceMenuItems[attendanceMenuIndex]),
    1
  );
}

void handleAttendanceMenu(byte command) {

  if (command == BUTTON_UP) {

    if (attendanceMenuIndex == 0) {
      attendanceMenuIndex = ATTENDANCE_MENU_ITEMS - 1;
    } else {
      attendanceMenuIndex--;
    }

    navigationBeep();
    showAttendanceMenu();
  }

  else if (command == BUTTON_DOWN) {

    attendanceMenuIndex++;

    if (attendanceMenuIndex >= ATTENDANCE_MENU_ITEMS) {
      attendanceMenuIndex = 0;
    }

    navigationBeep();
    showAttendanceMenu();
  }

  else if (command == BUTTON_OK) {

    if (attendanceMenuIndex == 0) {
      startAttendanceSession();
    }

    else if (attendanceMenuIndex == 1) {
      endAttendanceSession();
    }

    else if (attendanceMenuIndex == 2) {
      showLiveCount();
    }

    else if (attendanceMenuIndex == 3) {
      resetOpenedFromAttendanceMenu = true;
      currentScreen = SCREEN_RESET_CONFIRM;
      showResetConfirmation();
    }
  }

  else if (command == BUTTON_BACK) {
    currentScreen = SCREEN_MAIN_MENU;
    mainMenuIndex = 0;

    navigationBeep();
    showMainMenu();
  }
}

// =====================================================
// ATTENDANCE SESSION
// =====================================================

void startAttendanceSession() {

  if (attendanceActive) {
    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print(F("Session Already"));

    lcd.setCursor(0, 1);
    lcd.print(F("Running"));

    warningBeep();
    delay(1400);
  }

  else {
    attendanceActive = true;

    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print(F("Session Started"));

    lcd.setCursor(0, 1);
    lcd.print(F("Scan RFID Card"));

    successBeep();

    Serial.println(F("Attendance session started"));

    delay(1300);
  }

  showAttendanceScanScreen();
}

void endAttendanceSession() {

  if (!attendanceActive) {
    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print(F("No Active"));

    lcd.setCursor(0, 1);
    lcd.print(F("Session"));

    warningBeep();
    delay(1400);

    showAttendanceMenu();
    return;
  }

  attendanceActive = false;

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print(F("Session Ended"));

  lcd.setCursor(0, 1);
  lcd.print(F("Present:"));

  lcd.print(totalPresent);
  lcd.print("/");
  lcd.print(TOTAL_STUDENTS);

  successBeep();

  printAttendanceReport();

  delay(2000);

  showAttendanceMenu();
}

void showAttendanceScanScreen() {
  currentScreen = SCREEN_SCAN_ATTENDANCE;

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print(F("Scan RFID Card"));

  lcd.setCursor(0, 1);
  lcd.print(F("Present:"));

  lcd.print(totalPresent);
  lcd.print("/");
  lcd.print(TOTAL_STUDENTS);
}

void handleAttendanceScreen(byte command) {

  if (command == BUTTON_BACK) {
    attendanceMenuIndex = 0;

    navigationBeep();
    showAttendanceMenu();
  }

  else if (command == BUTTON_OK) {
    showLiveCount();
  }
}

void showLiveCount() {
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print(F("Present Count"));

  lcd.setCursor(0, 1);
  lcd.print(totalPresent);

  lcd.print("/");
  lcd.print(TOTAL_STUDENTS);

  successBeep();
  delay(1800);

  showAttendanceMenu();
}

// =====================================================
// RFID READING
// =====================================================

void checkRFID() {

  if (millis() - lastRFIDScan < RFID_SCAN_DELAY) {
    return;
  }

  if (!rfid.PICC_IsNewCardPresent()) {
    return;
  }

  if (!rfid.PICC_ReadCardSerial()) {
    return;
  }

  lastRFIDScan = millis();

  String scannedUID = getCardUID();

  Serial.print(F("Scanned UID: "));
  Serial.println(scannedUID);

  int studentIndex = findStudent(scannedUID);

  if (studentIndex == -1) {
    showUnknownCard(scannedUID);
  } else {
    markStudentPresent(studentIndex);
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

String getCardUID() {
  String uid = "";

  for (byte i = 0; i < rfid.uid.size; i++) {

    if (i > 0) {
      uid += " ";
    }

    if (rfid.uid.uidByte[i] < 0x10) {
      uid += "0";
    }

    uid += String(rfid.uid.uidByte[i], HEX);
  }

  uid.toUpperCase();

  return uid;
}

int findStudent(String scannedUID) {

  for (byte i = 0; i < TOTAL_STUDENTS; i++) {

    if (scannedUID == studentUID[i]) {
      return i;
    }
  }

  return -1;
}

void markStudentPresent(byte index) {
  lcd.clear();

  lcd.setCursor(0, 0);
  printLCDText(studentName[index]);

  if (studentPresent[index]) {
    lcd.setCursor(0, 1);
    lcd.print(F("Already Present"));

    warningBeep();

    Serial.print(studentName[index]);
    Serial.println(F(" already marked PRESENT"));
  }

  else {
    studentPresent[index] = true;
    totalPresent++;

    lcd.setCursor(0, 1);
    lcd.print(F("Attendance Done"));

    successBeep();

    Serial.print(studentName[index]);
    Serial.println(F(" marked PRESENT"));
  }

  delay(1600);

  if (attendanceActive) {
    showAttendanceScanScreen();
  }
}

void showUnknownCard(String uid) {
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print(F("Unknown Card"));

  lcd.setCursor(0, 1);
  lcd.print(F("Not Registered"));

  errorBeep();

  Serial.print(F("Unknown UID: "));
  Serial.println(uid);

  delay(1600);

  showAttendanceScanScreen();
}

// =====================================================
// STUDENT RECORDS
// =====================================================

void showStudentRecord() {
  currentScreen = SCREEN_STUDENT_RECORDS;

  lcd.clear();

  lcd.setCursor(0, 0);

  lcd.print(selectedStudent + 1);
  lcd.print(".");

  printLCDText(studentName[selectedStudent], 2);

  lcd.setCursor(0, 1);

  if (studentPresent[selectedStudent]) {
    lcd.print(F("Status: PRESENT"));
  } else {
    lcd.print(F("Status: ABSENT"));
  }
}

void handleStudentRecords(byte command) {

  if (command == BUTTON_RIGHT || command == BUTTON_DOWN) {

    selectedStudent++;

    if (selectedStudent >= TOTAL_STUDENTS) {
      selectedStudent = 0;
    }

    navigationBeep();
    showStudentRecord();
  }

  else if (command == BUTTON_LEFT || command == BUTTON_UP) {

    if (selectedStudent == 0) {
      selectedStudent = TOTAL_STUDENTS - 1;
    } else {
      selectedStudent--;
    }

    navigationBeep();
    showStudentRecord();
  }

  else if (command == BUTTON_BACK) {
    currentScreen = SCREEN_MAIN_MENU;
    mainMenuIndex = 1;

    navigationBeep();
    showMainMenu();
  }
}

// =====================================================
// REPORT
// =====================================================

void showReport() {
  currentScreen = SCREEN_REPORT;

  byte absentCount = TOTAL_STUDENTS - totalPresent;

  int percentage = 0;

  if (TOTAL_STUDENTS > 0) {
    percentage =
      ((int)totalPresent * 100) / TOTAL_STUDENTS;
  }

  lcd.clear();

  lcd.setCursor(0, 0);

  lcd.print(F("P:"));
  lcd.print(totalPresent);

  lcd.print(F(" A:"));
  lcd.print(absentCount);

  lcd.setCursor(0, 1);

  lcd.print(F("Attend:"));
  lcd.print(percentage);
  lcd.print("%");
}

void handleReportScreen(byte command) {

  if (command == BUTTON_OK) {
    printAttendanceReport();

    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print(F("Report Sent"));

    lcd.setCursor(0, 1);
    lcd.print(F("Serial Monitor"));

    successBeep();
    delay(1500);

    showReport();
  }

  else if (command == BUTTON_BACK) {
    currentScreen = SCREEN_MAIN_MENU;
    mainMenuIndex = 2;

    navigationBeep();
    showMainMenu();
  }
}

void printAttendanceReport() {
  Serial.println();
  Serial.println(F("=============================="));
  Serial.println(F("      ATTENDANCE REPORT"));
  Serial.println(F("=============================="));
  Serial.println(F("No,Name,UID,Status"));

  for (byte i = 0; i < TOTAL_STUDENTS; i++) {

    Serial.print(i + 1);
    Serial.print(",");

    Serial.print(studentName[i]);
    Serial.print(",");

    Serial.print(studentUID[i]);
    Serial.print(",");

    if (studentPresent[i]) {
      Serial.println(F("PRESENT"));
    } else {
      Serial.println(F("ABSENT"));
    }
  }

  Serial.println(F("------------------------------"));

  Serial.print(F("Total Present: "));
  Serial.print(totalPresent);
  Serial.print("/");
  Serial.println(TOTAL_STUDENTS);

  Serial.println(F("=============================="));
  Serial.println();
}

// =====================================================
// RESET ATTENDANCE
// =====================================================

void showResetConfirmation() {
  currentScreen = SCREEN_RESET_CONFIRM;

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print(F("Reset Records?"));

  lcd.setCursor(0, 1);
  lcd.print(F("OK=Yes BACK=No"));
}

void handleResetConfirmation(byte command) {

  if (command == BUTTON_OK) {
    resetAttendance();
  }

  else if (command == BUTTON_BACK) {
    navigationBeep();

    if (resetOpenedFromAttendanceMenu) {
      attendanceMenuIndex = 3;
      showAttendanceMenu();
    } else {
      showHomeScreen();
    }
  }
}

void resetAttendance() {

  for (byte i = 0; i < TOTAL_STUDENTS; i++) {
    studentPresent[i] = false;
  }

  totalPresent = 0;
  attendanceActive = false;

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print(F("Attendance"));

  lcd.setCursor(0, 1);
  lcd.print(F("Reset Complete"));

  successBeep();

  Serial.println(F("Attendance records reset"));

  delay(1600);

  if (resetOpenedFromAttendanceMenu) {
    attendanceMenuIndex = 0;
    showAttendanceMenu();
  } else {
    showHomeScreen();
  }

  resetOpenedFromAttendanceMenu = false;
}

// =====================================================
// BUZZER
// =====================================================

void toggleBuzzer() {
  buzzerEnabled = !buzzerEnabled;

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print(F("Buzzer"));

  lcd.setCursor(0, 1);

  if (buzzerEnabled) {
    lcd.print(F("Unmuted"));
    successBeep();
  } else {
    lcd.print(F("Muted"));
    digitalWrite(BUZZER_PIN, LOW);
  }

  delay(1200);

  redrawCurrentScreen();
}

void navigationBeep() {

  if (!buzzerEnabled) {
    return;
  }

  digitalWrite(BUZZER_PIN, HIGH);
  delay(35);
  digitalWrite(BUZZER_PIN, LOW);
}

void successBeep() {

  if (!buzzerEnabled) {
    return;
  }

  digitalWrite(BUZZER_PIN, HIGH);
  delay(100);
  digitalWrite(BUZZER_PIN, LOW);
}

void warningBeep() {

  if (!buzzerEnabled) {
    return;
  }

  for (byte i = 0; i < 2; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);

    digitalWrite(BUZZER_PIN, LOW);
    delay(100);
  }
}

void errorBeep() {

  if (!buzzerEnabled) {
    return;
  }

  for (byte i = 0; i < 3; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(80);

    digitalWrite(BUZZER_PIN, LOW);
    delay(80);
  }
}

void shutdownBeep() {

  if (!buzzerEnabled) {
    return;
  }

  digitalWrite(BUZZER_PIN, HIGH);
  delay(250);
  digitalWrite(BUZZER_PIN, LOW);
}

// =====================================================
// LCD HELPERS
// =====================================================

void redrawCurrentScreen() {

  switch (currentScreen) {

    case SCREEN_HOME:
      showHomeScreen();
      break;

    case SCREEN_MAIN_MENU:
      showMainMenu();
      break;

    case SCREEN_ATTENDANCE_MENU:
      showAttendanceMenu();
      break;

    case SCREEN_SCAN_ATTENDANCE:
      showAttendanceScanScreen();
      break;

    case SCREEN_STUDENT_RECORDS:
      showStudentRecord();
      break;

    case SCREEN_REPORT:
      showReport();
      break;

    case SCREEN_RESET_CONFIRM:
      showResetConfirmation();
      break;

    case SCREEN_OFF:
      showOffScreen();
      break;
  }
}

void printLCDText(String text, byte startColumn) {

  if (startColumn >= 16) {
    return;
  }

  byte availableCharacters = 16 - startColumn;

  for (
    byte i = 0;
    i < text.length() && i < availableCharacters;
    i++
  ) {
    lcd.print(text[i]);
  }
}
