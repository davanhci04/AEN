#include <ESP8266WiFi.h>
#include "FirebaseESP8266.h"
#include <Keypad.h>
#include <Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#define WIFI_SSID "ok"
#define WIFI_PASSWORD "12345678"
#define FIREBASE_HOST "henhung-82bcc-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "0k312kCuSFTYsEM7TaelCCJ7418tnyPyA3P6DKOD"

// Địa chỉ I2C và kích thước LCD (thường 16x2 hoặc 20x4)
#define LCD_ADDRESS 0x27  // Thay đổi nếu địa chỉ I2C khác (ví dụ: 0x3F)
#define LCD_COLS 16
#define LCD_ROWS 2

FirebaseData firebaseData;
FirebaseConfig config;
FirebaseAuth auth;
String path = "/";

const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', 'D', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};
byte rowPins[ROWS] = {D1, D2, D3, D4}; // GPIO 5, 4, 0, 2
byte colPins[COLS] = {D5, D6, D7, D8}; // GPIO 14, 12, 13, 15
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

Servo doorServo;
#define SERVO_PIN D0
#define CLOSE_POS 0
#define OPEN_POS 90

LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLS, LCD_ROWS);

String inputPassword = "";
int correctPassword = 0;      // Mật khẩu từ Firebase (online)
int localPassword = 0;        // Mật khẩu cục bộ (offline, lưu mật khẩu gần nhất)
int wrongAttempts = 0;
bool doorOpen = false;
unsigned long lastOpenTime = 0;

int changePasswordStep = 1;
String newPassword = "";
String confirmPassword = "";
String oldPassword = "";
bool changePasswordMode = false;
bool firebaseConnected = false; // Theo dõi trạng thái kết nối Firebase

// Thêm cho mật khẩu tạm thời
int tempPassword = -1;         // Mật khẩu tạm thời (OTP), -1 là chưa có
unsigned long tempPasswordTimestamp = 0; // Thời điểm tạo mật khẩu tạm thời
const unsigned long TEMP_PASSWORD_DURATION = 60000; // 1 phút (60,000ms)

void setup() {
    Serial.begin(115200);
    randomSeed(analogRead(A0)); // Khởi tạo random dựa trên noise analog

    // Khởi động LCD I2C
    lcd.begin(LCD_COLS, LCD_ROWS); // Sử dụng begin với số cột và dòng
    lcd.backlight();
    displayWelcome();

    Wire.begin(D2, D1); // Khởi động I2C với SDA (D2) và SCL (D1)

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Đang kết nối WiFi");
    lcd.setCursor(0, 0);
    lcd.print("Ket noi WiFi...");
    lcd.setCursor(1, 0);
    lcd.print("---------");
    
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        lcd.setCursor(13, 0);
        lcd.print(".");
    }
    Serial.println("\n=== WiFi đã kết nối thành công! ===");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi OK! IP:");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP().toString());

    config.host = FIREBASE_HOST;
    config.signer.tokens.legacy_token = FIREBASE_AUTH;
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    doorServo.attach(SERVO_PIN);
    doorServo.write(CLOSE_POS);

    // Thử kết nối và lấy mật khẩu từ Firebase với retry
    int retryCount = 0;
    const int maxRetries = 5;
    while (retryCount < maxRetries) {
        if (Firebase.ready()) {
            firebaseConnected = true;
            if (Firebase.getString(firebaseData, path + "/password")) {
                correctPassword = firebaseData.intData();
                localPassword = correctPassword; // Đồng bộ localPassword với Firebase
                Serial.println("=== Mật khẩu ban đầu từ Firebase: " + String(correctPassword) + " ===");
            } else {
                Serial.println("!!! Lỗi lấy mật khẩu: " + firebaseData.errorReason() + " - Sử dụng mật khẩu cục bộ: " + String(localPassword) + " !!!");
            }
            if (Firebase.getBool(firebaseData, path + "/doorState")) {
                doorOpen = firebaseData.boolData();
                Serial.println("=== Trạng thái cửa từ Firebase: " + String(doorOpen ? "Mở" : "Đóng") + " ===");
            } else {
                Serial.println("!!! Lỗi lấy trạng thái cửa: " + firebaseData.errorReason() + " - Giả sử cửa đóng (false) !!!");
                doorOpen = false;
            }
            break;
        } else {
            Serial.println("!!! Firebase chưa sẵn sàng - Thử lại lần " + String(retryCount + 1) + " - Sử dụng mật khẩu cục bộ: " + String(localPassword) + " !!!");
            delay(1000);
            retryCount++;
        }
    }
    if (retryCount >= maxRetries) {
        firebaseConnected = false;
        Serial.println("!!! Không thể kết nối Firebase sau " + String(maxRetries) + " lần thử. Sử dụng mật khẩu cục bộ: " + String(localPassword) + " !!!");
    }

    displayInstructions();
}

void loop() {
    

    char key = keypad.getKey();

    if (key) {
        Serial.print("Phím nhấn: ");
        Serial.println(key);

        if (key == 'C') {
            if (changePasswordMode) {
                // Thoát chế độ đổi mật khẩu nếu đang ở chế độ này
                changePasswordMode = false;
                changePasswordStep = 1;
                oldPassword = "";
                newPassword = "";
                confirmPassword = "";
                Serial.println("=== Đã thoát chế độ đổi mật khẩu. Hệ thống trở lại chế độ bình thường. ===");
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("Da thoat che do");
                lcd.setCursor(0, 1);
                lcd.print("doi MK!");
                delay(2000);
                displayInstructions();
            } else {
                // Vào chế độ đổi mật khẩu nếu chưa ở chế độ này
                changePasswordMode = true;
                changePasswordStep = 1;
                oldPassword = "";
                newPassword = "";
                confirmPassword = "";
                Serial.println("=== Đã vào chế độ đổi mật khẩu ===");
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("Nhap MK cu:    ");
                lcd.setCursor(0, 1);
                lcd.print("Bam # de xac nhan");
            }
        } else if (key == 'D') { // Thêm phím D để tạo mật khẩu tạm thời
            generateTempPassword();
            displayTempPassword();
        } else if (key == '#') {
            if (changePasswordMode) {
                changePassword();
            } else {
                checkPassword();
            }
        } else if (key == '*') {
            inputPassword = "";
            if (changePasswordMode) {
                oldPassword = "";
                newPassword = "";
                confirmPassword = "";
                Serial.println("--- Đã xóa toàn bộ dữ liệu vừa nhập ---");
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("Da xoa du lieu!");
                delay(2000);
                if (changePasswordStep == 1) {
                    lcd.clear();
                    lcd.setCursor(0, 0);
                    lcd.print("Nhap MK cu:    ");
                    lcd.setCursor(0, 1);
                    lcd.print("Bam # de xac nhan");
                } else if (changePasswordStep == 2) {
                    lcd.clear();
                    lcd.setCursor(0, 0);
                    lcd.print("Nhap MK moi:   ");
                    lcd.setCursor(0, 1);
                    lcd.print("Bam # de xac nhan");
                } else if (changePasswordStep == 3) {
                    lcd.clear();
                    lcd.setCursor(0, 0);
                    lcd.print("Xac nhan MK moi:");
                    lcd.setCursor(0, 1);
                    lcd.print("Bam # de xac nhan");
                }
            } else {
                Serial.println("--- Đã xóa mật khẩu vừa nhập ---");
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("Da xoa MK nhap!");
                delay(2000);
                displayInstructions();
            }
        } else if (!changePasswordMode && inputPassword.length() == 0) {
            // Xóa LCD và bắt đầu hiển thị '*' khi nhập mật khẩu chính
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Nhap MK: ");
            inputPassword += key;
            Serial.println("Đã nhập: " + inputPassword + " (" + String(inputPassword.length()) + "/4)");
            lcd.setCursor(9 + inputPassword.length() - 1, 0);
            lcd.print("*");
        } else if (!changePasswordMode && inputPassword.length() < 4) {
            // Tiếp tục hiển thị '*' cho các ký tự tiếp theo
            inputPassword += key;
            Serial.println("Đã nhập: " + inputPassword + " (" + String(inputPassword.length()) + "/4)");
            lcd.setCursor(6 + inputPassword.length() - 1, 0);
            lcd.print("*");
        } else if (changePasswordMode && changePasswordStep == 1 && oldPassword.length() == 0) {
            // Xóa LCD và bắt đầu hiển thị '*' khi nhập mật khẩu cũ
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("MK cu: ");
            oldPassword += key;
            Serial.println("Mật khẩu cũ: " + oldPassword + " (" + String(oldPassword.length()) + "/4)");
            lcd.setCursor(7 + oldPassword.length() - 1, 0);
            lcd.print("*");
        } else if (changePasswordMode && changePasswordStep == 1 && oldPassword.length() < 4) {
            // Tiếp tục hiển thị '*' cho mật khẩu cũ
            oldPassword += key;
            Serial.println("Mật khẩu cũ: " + oldPassword + " (" + String(oldPassword.length()) + "/4)");
            lcd.setCursor(7 + oldPassword.length() - 1, 0);
            lcd.print("*");
        } else if (changePasswordMode && changePasswordStep == 2 && newPassword.length() == 0) {
            // Xóa LCD và bắt đầu hiển thị '*' khi nhập mật khẩu mới
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("MK moi: ");
            newPassword += key;
            Serial.println("Mật khẩu mới: " + newPassword + " (" + String(newPassword.length()) + "/4)");
            lcd.setCursor(8 + newPassword.length() - 1, 0);
            lcd.print("*");
            lcd.setCursor(0, 1);
            lcd.print("Bam # de xac nhan");
        } else if (changePasswordMode && changePasswordStep == 2 && newPassword.length() < 4) {
            // Tiếp tục hiển thị '*' cho mật khẩu mới
            newPassword += key;
            Serial.println("Mật khẩu mới: " + newPassword + " (" + String(newPassword.length()) + "/4)");
            lcd.setCursor(8 + newPassword.length() - 1, 0);
            lcd.print("*");
        } else if (changePasswordMode && changePasswordStep == 3 && confirmPassword.length() == 0) {
            // Xóa LCD và bắt đầu hiển thị '*' khi nhập xác nhận mật khẩu
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Xac nhan MK: ");
            confirmPassword += key;
            Serial.println("Xác nhận mật khẩu: " + confirmPassword + " (" + String(confirmPassword.length()) + "/4)");
            lcd.setCursor(12 + confirmPassword.length() - 1, 0);
            lcd.print("*");
            lcd.setCursor(0, 1);
            lcd.print("Bam # de xac nhan");
        } else if (changePasswordMode && changePasswordStep == 3 && confirmPassword.length() < 4) {
            // Tiếp tục hiển thị '*' cho xác nhận mật khẩu
            confirmPassword += key;
            Serial.println("Xác nhận mật khẩu: " + confirmPassword + " (" + String(confirmPassword.length()) + "/4)");
            lcd.setCursor(12 + confirmPassword.length() - 1, 0);
            lcd.print("*");
        }
    }

    if (doorOpen && millis() - lastOpenTime > 10000) {
        Serial.println("=== Cửa đã mở quá 10 giây, tự động đóng lại ===");
        closeDoor();
        displayDoorStatus();
    }

    // Kiểm tra hết hạn mật khẩu tạm thời
    checkTempPasswordExpiration();

    // Kiểm tra kết nối Firebase định kỳ
    static unsigned long lastFirebaseCheck = 0;
    if (millis() - lastFirebaseCheck > 5000) { // Kiểm tra mỗi 5 giây
        if (Firebase.ready() && !firebaseConnected) {
            firebaseConnected = true;
            Serial.println("=== Firebase đã kết nối lại! Cập nhật dữ liệu... ===");
            syncWithFirebase();
        } else if (!Firebase.ready() && firebaseConnected) {
            firebaseConnected = false;
            Serial.println("!!! Mất kết nối Firebase - Sử dụng mật khẩu cục bộ: " + String(localPassword) + " !!!");
        }
        lastFirebaseCheck = millis();
    }
}

void checkPassword() {
    if (inputPassword.length() != 4) {
        Serial.println("!!! Lỗi: Mật khẩu phải đủ 4 số. Bạn đã nhập: " + inputPassword + " !!!");
        inputPassword = "";
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("MK sai do dai!");
        delay(2000);
        displayInstructions();
        return;
    }

    Serial.println("--- Đang kiểm tra mật khẩu: " + inputPassword + " ---");
    int passwordToCheck = firebaseConnected ? correctPassword : localPassword;
    String correctPasswordStr = String(passwordToCheck);

    // Kiểm tra mật khẩu tạm thời trước
    if (tempPassword != -1 && isTempPasswordValid() && inputPassword == String(tempPassword)) {
        Serial.println("=== Mật khẩu tạm thời đúng! Cửa đang mở... ===");
        openDoor();
        tempPassword = -1; // Xóa mật khẩu tạm thời sau khi sử dụng
        wrongAttempts = 0;
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Cua da mo!");
        delay(2000);
        displayDoorStatus();
    }
    // Kiểm tra mật khẩu chính
    else if (inputPassword == correctPasswordStr) {
        Serial.println("=== Mật khẩu chính đúng! Cửa đang mở... ===");
        openDoor();
        wrongAttempts = 0;

        // Kiểm tra và cập nhật localPassword nếu khác
        if (localPassword != passwordToCheck) {
            localPassword = passwordToCheck;
            Serial.println("=== Đã cập nhật localPassword để đồng bộ với mật khẩu đúng: " + String(localPassword) + " ===");
        }
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Cua da mo!");
        delay(2000);
        displayDoorStatus();
    } else {
        wrongAttempts++;
        Serial.println("!!! Mật khẩu sai! Còn " + String(5 - wrongAttempts) + " lần thử trước khi báo động !!!");
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("MK sai!");
        delay(2000);
        if (wrongAttempts >= 5) {
            Serial.println("=== CẢNH BÁO: Nhập sai quá 5 lần! Đã gửi thông báo đến Firebase ===");
            if (firebaseConnected) {
                Firebase.setString(firebaseData, path + "/alert", "Cảnh báo: Nhập sai quá 5 lần!");
            }
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Canh bao 5 lan!");
            delay(2000);
        }
        displayInstructions();
    }
    inputPassword = "";
}

void changePassword() {
    if (changePasswordStep == 1 && oldPassword.length() == 4) {
        int passwordToCheck = firebaseConnected ? correctPassword : localPassword;
        if (oldPassword == String(passwordToCheck)) {
            Serial.println("=== Mật khẩu cũ đúng! ===");
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("MK cu dung!");
            delay(2000);
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Nhap MK moi:   ");
            lcd.setCursor(0, 1);
            lcd.print("Bam # de xac nhan");
            changePasswordStep = 2;
        } else {
            Serial.println("!!! Mật khẩu cũ sai! Vui lòng nhập lại. !!!");
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("MK cu sai!");
            delay(2000);
            oldPassword = "";
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Nhap MK cu:    ");
            lcd.setCursor(0, 1);
            lcd.print("Bam # de xac nhan");
        }
    } else if (changePasswordStep == 2 && newPassword.length() == 4) {
        Serial.println("--- Mật khẩu mới đã nhập: " + newPassword + " ---");
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Xac nhan MK moi:");
        lcd.setCursor(0, 1);
        lcd.print("Bam # de xac nhan");
        changePasswordStep = 3;
    } else if (changePasswordStep == 3 && confirmPassword.length() == 4) {
        if (newPassword == confirmPassword) {
            correctPassword = newPassword.toInt();
            localPassword = correctPassword; // Cập nhật localPassword khi đổi mật khẩu
            if (firebaseConnected) {
                if (Firebase.setInt(firebaseData, path + "/password", correctPassword)) {
                    Serial.println("=== Mật khẩu đã được thay đổi thành công thành: " + String(correctPassword) + " ===");
                    lcd.clear();
                    lcd.setCursor(0, 0);
                    lcd.print("MK da doi thanh cong");
                    delay(2000);
                    Serial.println("Hệ thống trở lại chế độ bình thường.");
                    changePasswordMode = false;
                    changePasswordStep = 1;
                    wrongAttempts = 0;
                    displayInstructions();
                } else {
                    Serial.println("!!! Lỗi: Không thể cập nhật mật khẩu lên Firebase - " + firebaseData.errorReason() + " !!!");
                    lcd.clear();
                    lcd.setCursor(0, 0);
                    lcd.print("Loi Firebase!");
                    delay(2000);
                    lcd.clear();
                    lcd.setCursor(0, 0);
                    lcd.print("Xac nhan MK moi:");
                    lcd.setCursor(0, 1);
                    lcd.print("Bam # de xac nhan");
                }
            } else {
                Serial.println("=== Mật khẩu đã được thay đổi thành công cục bộ: " + String(localPassword) + " (sẽ đồng bộ khi kết nối Firebase) ===");
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("MK doi cuc bo:");
                lcd.setCursor(0, 1);
                lcd.print(String(localPassword));
                delay(2000);
                changePasswordMode = false;
                changePasswordStep = 1;
                wrongAttempts = 0;
                displayInstructions();
            }
        } else {
            Serial.println("!!! Xác nhận mật khẩu không khớp! Vui lòng nhập lại từ bước 2. !!!");
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("MK khong khop!");
            delay(2000);
            newPassword = "";
            confirmPassword = "";
            changePasswordStep = 2;
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Nhap MK moi:   ");
            lcd.setCursor(0, 1);
            lcd.print("Bam # de xac nhan");
        }
    }
}

void openDoor() {
    doorServo.write(OPEN_POS);
    doorOpen = true;
    lastOpenTime = millis();
    if (firebaseConnected) {
        Firebase.setBool(firebaseData, path + "/doorState", true);
    }
    Serial.println("--- Cửa đã mở ---");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Cua da mo!");
    delay(2000);
    displayDoorStatus();
}

void closeDoor() {
    doorServo.write(CLOSE_POS);
    doorOpen = false;
    if (firebaseConnected) {
        Firebase.setBool(firebaseData, path + "/doorState", false);
    }
    Serial.println("--- Cửa đã đóng ---");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Cua da dong!");
    delay(2000);
    displayDoorStatus();
}

void syncWithFirebase() {
    if (Firebase.ready()) {
        if (Firebase.setInt(firebaseData, path + "/password", correctPassword)) {
            Serial.println("=== Đã đồng bộ mật khẩu lên Firebase: " + String(correctPassword) + " ===");
        } else {
            Serial.println("!!! Lỗi đồng bộ mật khẩu: " + firebaseData.errorReason() + " !!!");
        }
        Firebase.setBool(firebaseData, path + "/doorState", doorOpen);
        Serial.println("=== Đã đồng bộ trạng thái cửa lên Firebase: " + String(doorOpen ? "Mở" : "Đóng") + " ===");
    }
}

void generateTempPassword() {
    if (tempPassword != -1 && isTempPasswordValid()) {
        Serial.println("!!! Mật khẩu tạm thời hiện tại vẫn còn hiệu lực (" + String((TEMP_PASSWORD_DURATION - (millis() - tempPasswordTimestamp)) / 1000) + " giây) !!!");
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("MK tam con hop");
        lcd.setCursor(0, 1);
        lcd.print("le: ");
        lcd.print((TEMP_PASSWORD_DURATION - (millis() - tempPasswordTimestamp)) / 1000);
        lcd.print("s");
        delay(2000);
        return;
    }
    tempPassword = random(0, 10000); // Tạo mật khẩu ngẫu nhiên 4 chữ số
    tempPasswordTimestamp = millis(); // Ghi lại thời điểm tạo
    Serial.println("=== Đã tạo mật khẩu tạm thời: " + String(tempPassword) + " (hiệu lực 1 phút) ===");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("MK tam: ");
    lcd.print(tempPassword);
    lcd.setCursor(0, 1);
    lcd.print("Het han sau 1p");
    delay(2000);
    displayInstructions();
}

void checkTempPasswordExpiration() {
    if (tempPassword != -1 && millis() - tempPasswordTimestamp > TEMP_PASSWORD_DURATION) {
        Serial.println("=== Mật khẩu tạm thời " + String(tempPassword) + " đã hết hạn !!!");
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("MK tam da het!");
        delay(2000);
        tempPassword = -1; // Xóa mật khẩu tạm thời khi hết hạn
        displayInstructions();
    }
}

bool isTempPasswordValid() {
    return tempPassword != -1 && (millis() - tempPasswordTimestamp) <= TEMP_PASSWORD_DURATION;
}

void displayWelcome() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("He thong cua");
    lcd.setCursor(0, 1);
    lcd.print("thong minh");
    delay(2000);
}

void displayInstructions() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Nhap MK: ");
    lcd.setCursor(0, 1);
    lcd.print("Bam # mo cua");
}

void displayDoorStatus() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Trang thai cua:");
    lcd.setCursor(0, 1);
    lcd.print(doorOpen ? "Mo" : "Dong");
}

void displayTempPassword() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("MK tam: ");
    lcd.print(tempPassword);
    lcd.setCursor(0, 1);
    lcd.print("Het han sau 1p");
    delay(2000);
    displayInstructions();
}