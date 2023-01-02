//SDA = A4, SCL = A5

//傳送資料到ESP8266
#include <SoftwareSerial.h>
SoftwareSerial toESP(3, 2); // 接到(D4,D3)

//DS3231時鐘 (L2C address自動匹配)
#include <RTClib.h>
RTC_DS3231 rtc;
const char daysOfTheWeek[7][12] = { "日", "一", "二", "三", "四", "五", "六" };


//TOTP

#include <TOTP.h>
uint8_t hmacKey[5] = { };
TOTP totp = TOTP(hmacKey, 5);
String code[7];


//LCD1602螢幕
#include <LiquidCrystal_PCF8574.h>
LiquidCrystal_PCF8574 lcd(0x27);    //0x25為A1已焊接,全無焊接則為0x27 (L2C address)

//SSD1306螢幕& QR code
#include <U8g2lib.h>
#include "qrcode.h"
U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* SCL=*/ 21, /* SDA=*/ 20);


//4x4矩陣鍵盤
#include <Keypad.h>;
#define KEY_ROWS 4 // 按鍵模組列數(橫)
#define KEY_COLS 4 // 按鍵模組行數(直)
const char keymap[KEY_ROWS][KEY_COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};
const byte colPins[KEY_COLS] = { 9, 8, 7, 6 };     // 按鍵面朝內 左13~6右
const byte rowPins[KEY_ROWS] = { 13, 12, 11, 10 };
Keypad myKeypad = Keypad(makeKeymap(keymap), rowPins, colPins, KEY_ROWS, KEY_COLS);	// 初始化Keypad物件, 語法：Keypad(makeKeymap(按鍵字元的二維陣列), 模組列接腳, 模組行接腳, 模組列數, 模組行數)


//NFC (PN532) (I2C)	   
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_PN532.h>
#define IRQ_Pin 5
#define RST0_Pin 4
Adafruit_PN532 nfc(IRQ_Pin, RST0_Pin);


//base32			
#define MAX_STRING_LENGTH 10 // 必須是5的倍數
//RAM 消耗大約是這個值的 2.7 倍字節 

// 設置兩個字串緩衝區陣列的長度
#define     RAWSTR_BUFFER_BYTES     (MAX_STRING_LENGTH + 1)
#define     BASE32_BUFFER_BYTES     (MAX_STRING_LENGTH / 5 * 8 + 1)
char rawstr[RAWSTR_BUFFER_BYTES];
char base32[BASE32_BUFFER_BYTES];


void encodeBase32(char* b32, const char* str) {	   //明文to Base32
	unsigned char fin = 0, bits;
	unsigned int bcnt = 0, scnt = 0;
	while (bcnt < BASE32_BUFFER_BYTES) {
		bits = *(str + scnt);
		if (!bits) break;
		fin = (fin + 5) & 7;
		if (fin >= 5) { bits >>= 8 - fin; }
		else { bits <<= fin; bits |= *(str + ++scnt) >> (8 - fin); }
		bits &= ~0xE0; bits += bits < 26 ? 0x41 : 0x18;
		*(b32 + bcnt++) = bits;
	}
	for (scnt = 8; scnt < bcnt; scnt += 8);
	while (bcnt < scnt) *(b32 + bcnt++) = '=';
	while (bcnt < BASE32_BUFFER_BYTES) *(b32 + bcnt++) = 0;
}
//Base32 to 明文
/*
void decodeBase32(char* str, const char* b32) {
	unsigned char fin = 0, bits;
	unsigned int scnt = 0, bcnt = 0, buff = 0;
	while (bcnt < BASE32_BUFFER_BYTES) {
		bits = *(b32 + bcnt++);
		if (!bits || bits == '=') break;
		bits -= bits > 0x40 ? 0x41 : 0x18;
		buff <<= 5;
		buff |= bits;
		fin += 5;
		if (fin >= 8) {
			fin -= 8;
			*(str + scnt++) = 0xFF & (buff >> fin);
		}
	}
	while (scnt < RAWSTR_BUFFER_BYTES) *(str + scnt++) = 0;
}
  */
  //EEPROM
#include <EEPROM.h>
int address = 0;


////////////////////////////自行宣告//////////////////////////////
void(*resetFunc) (void);	     //重開機
String Action, Success;

int i, x, frozen;
unsigned int done, ok;
unsigned long Waiting_Period;
uint8_t uid[] = { 0, 0, 0, 0 };
uint8_t tempUID[] = { 0, 0, 0, 0 };
uint8_t uidLength;


int step = 0, Register_step = -1, Login_step = -1;


//TOTP APP 連結
String choose;
#define Android "https://reurl.cc/aklm69"
#define iOS  "https://reurl.cc/k75eNn"


//驗證密碼
char keyIN[7];
String VerifyCode;
String hmacKeyBase32, hmacKeyString; //裝載base32 兩者數值相同
char hmacKeyArray[5] = {};


//帳戶
int Logining = 0;
String domain = "DAAN";
byte same;

#define MAX_SAVED 10
uint8_t saved_UID[MAX_SAVED][4];
char saved_PASSWORD[MAX_SAVED][5];


void setup() {
	Serial.begin(115200);
	toESP.begin(115200);



	////////////////////初始化LCD1602(LCD)//////////////////
	lcd.begin(20, 4);      // 初始化 20列4行
	lcd.setBacklight(HIGH); // 開啟or關閉背光
	lcd.home();            //指標設為原點
	lcd.clear();           //清除所有文字
	lcd.setCursor(0, 0);   //設定指標位置

	////////////////////尋找DS3231(時鐘)/////////////////////
	if (!rtc.begin()) {
		Serial.println(F("Can't find RTC!"));
		Serial.flush();
		lcd.setCursor(0, 0);
		frozen = 1;
	}
	///////////////////初始化SSD1306 (Qr code)///////////////
	u8g2.begin();
	////////////////////初始化&尋找PN532(NFC)/////////////////////
	nfc.begin();
	uint32_t versiondata = nfc.getFirmwareVersion();
	nfc.SAMConfig();
	if (!versiondata) {
		Serial.print(F("Can't find PN532!"));
		frozen = 1;
	}

	//if (rtc.lostPower())(rtc.adjust(DateTime(2021, 12, 4, 21, 23, 25)));
	//rtc.adjust(DateTime(2022, 1, 1, 19,0, 10));
	if (rtc.begin() && versiondata)(frozen = 0);			   //當有元件遺失,將阻止Arduino初始化
	if (frozen) {
		lcd.print(F("Failed to boot!"));
		resetFunc();
	}

	else(Serial.println(F("booted Arduino Successfully ")));
}


void loop() {
	//即時更新的資料//
	DateTime now = rtc.now();     //Arduino時間=DS3231時間

	char key = myKeypad.getKey(); //https://swf.com.tw/?p=917
	if (key) { Serial.print("按下的按鍵= "); Serial.println(key); }
	// 若有按鍵被按下 顯示按鍵的字元 但同時只能按一顆按鍵

	long GMT = now.unixtime() - 8 * 3600; //驗證碼(因台灣時區為GMT+8,所以-8小時)
	String code = totp.getCode(GMT);
	String old_code = totp.getCode(GMT - 10);

	//if (key == 'A')(resetFunc());
	//////////////////串列埠輸出驗證碼&時間, <10補0,日期不補///////////////////////
	//嚴重消耗效能!
	 /*
	if (millis() - LastScanDate >= 1000) {

		Serial.print("code= ");   Serial.println(code);

		Serial.print(now.year());
		Serial.print('/');
		Serial.print(now.month());
		Serial.print('/');
		Serial.print(now.day());
		Serial.print(" (");
		Serial.print(daysOfTheWeek[now.dayOfTheWeek()]);
		Serial.print(") ");

		if ((now.hour()) < 10)(Serial.print("0"));
		Serial.print(now.hour());
		Serial.print(':');
		if ((now.minute()) < 10)(Serial.print("0"));
		Serial.print(now.minute());
		Serial.print(':');
		if ((now.second()) < 10)(Serial.print("0"));
		Serial.println(now.second());
		Serial.println("-------------------");


	}
	*/

	///////////////////////////待機狀態//////////////////////////////////////
	if (step == 0) {
		done = 0;
		ok = 0;
		lcd.setCursor(0, 0);
		lcd.print(F("--Dynamic Password--"));
		lcd.setCursor(0, 1);
		lcd.print(F("---Control System---"));


		lcd.setCursor(1, 2);				   //輸出到LCD
		lcd.print(now.year() - 1911);		   //民國時間
		lcd.print('/');
		if ((now.month()) < 10)(lcd.print("0"));
		lcd.print(now.month());
		lcd.print('/');
		if ((now.day()) < 10)(lcd.print("0"));
		lcd.print(now.day());


		lcd.print(" ");
		if (now.hour() >= 12 && now.hour() != 24)(lcd.print(F("PM ")));
		else((lcd.print(F("AM "))));
		if (now.hour() < 10 || (now.hour() >= 13 && now.hour() - 12 < 10))(lcd.print(F("0")));
		if (now.hour() >= 13)(lcd.print(now.hour() - 12));
		else(lcd.print(now.hour()));
		lcd.print(F(":"));
		if ((now.minute()) < 10)(lcd.print(F("0")));
		lcd.print(now.minute());
		//操作提示
		lcd.setCursor(0, 3);						//第二行
		lcd.print(F(" Press <#> to Start"));
		if (key == '#') { lcd.clear();  step = 1; }
	}
	///////////////////////////選擇模式//////////////////////////////////////
	else if (step == 1) {
		lcd.setCursor(0, 0);
		lcd.print(F("      Welcome! "));
		lcd.setCursor(0, 1);
		lcd.print(F("Please select a mode"));

		lcd.setCursor(0, 2);
		lcd.print(F("   <A> : Login"));
		lcd.setCursor(0, 3);
		lcd.print(F("   <B> : Register"));

		Waiting_Period = millis();

		if (key == 'A') {				 //按A,登入
			key = '\0';
			lcd.clear();
			Login_step = 1;
			step = -1;
		}
		else if (key == 'B') {			 //按B,註冊
			key = '\0';
			lcd.clear();
			same = 0;
			done = 0;
			Register_step = 1;
			step = -1;
		}
	}
	/////////////////////////////註冊/////////////////////////////////////
	//讀卡
	if (Register_step == 1) {

		for (i = 0; i <= 4; i++) {
			tempUID[i], uid[i] = '\0';
		}
		lcd.setCursor(0, 1);					   //顯示等待訊息
		lcd.print(F("  Waiting to read"));
		lcd.setCursor(0, 2);
		lcd.print(F("   your NFC card"));
		uint8_t uid[] = { 0, 0, 0, 0 }; //NFC讀取

		bool success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, &uid[0], &uidLength);

		Serial.print(F("UID Value: "));
		for (i = 0; i <= 3; i++) {
			Serial.print(F(" 0x")); Serial.print(uid[i], HEX);
			tempUID[i] = uid[i];
		}
		Serial.println();
		byte j;

		for (i = 0; i <= address - 1; i++) {		  //查找重複註冊
			for (j = 0; j <= uidLength - 1; j++) {
				if (tempUID[j] == saved_UID[i][j]) {
					done++;
					if (done >= 4) {
						same = i;
						Serial.println("相同");
					}
				}
			}
		}
		if (done >= 4) {
			lcd.clear();
			lcd.setCursor(0, 1);
			lcd.print(F("   You have been"));
			lcd.setCursor(0, 2);
			lcd.print(F("     Registered"));
			Success = "失敗 (重複註冊)";
			toESP.print("taivs");
			if (i - 1 <= 10)(toESP.print('0'));
			if (i - 1 <= 100)(toESP.print('0'));
			toESP.print(i - 1);
			toESP.print("１");  /////

			toESP.print("0x");			toESP.print(tempUID[0], HEX);
			toESP.print(" 0x");			toESP.print(tempUID[1], HEX);
			toESP.print(" 0x");			toESP.print(tempUID[2], HEX);
			toESP.print(" 0x");			toESP.print(tempUID[3], HEX);
			toESP.print("２");  /////

			toESP.print("註冊");
			toESP.print("３");  /////

			toESP.print(Success);
			toESP.print("４");  /////


			toESP.print('\n');
			toESP.println("");
			delay(3300);
			lcd.clear();
			Register_step = -1;
			step = 0;
		}
		else if (done < 4 && same != 1) {
			lcd.clear();
			Register_step++;
		}

	}
	//選擇Android or iOS
	else if (Register_step == 2) {
		lcd.setCursor(0, 0);
		lcd.print(F("    Which system"));
		lcd.setCursor(0, 1);
		lcd.print(F("    do you use?"));

		lcd.setCursor(0, 2);
		lcd.print(F("   <A> : Android"));
		lcd.setCursor(0, 3);
		lcd.print(F("   <B> : iOS"));

		if (millis() - Waiting_Period >= 200) {
			if (key == 'A') {
				i = 0;
				choose = Android;
				lcd.clear();
				lcd.setCursor(0, 2);
				lcd.print(F("  in  Google Store"));
				Waiting_Period = millis();
				Register_step++;
			}
			else if (key == 'B') {
				i = 0;
				choose = iOS;
				lcd.clear();
				lcd.setCursor(0, 2);
				lcd.print(F("    in APP store"));
				Waiting_Period = millis();
				Register_step++;
			}
		}
	}
	//生成相對應之QR code
	else if (Register_step == 3) {
		lcd.setCursor(0, 0);
		lcd.print(F(" Scan the QRcode to"));
		lcd.setCursor(0, 1);
		lcd.print(F("install the TOTP APP"));

		lcd.setCursor(0, 3);
		lcd.print(F("Press<#> to Continue"));

		const char* URL = choose.c_str();
		for (; i == 0; i++) {
			QRCode qrcode;
			uint8_t qrcodeData[qrcode_getBufferSize(3)];
			qrcode_initText(&qrcode, qrcodeData, 3, ECC_LOW, URL);
			// 產生Qrcode
			u8g2.firstPage();
			do {
				// get the draw starting point,128 and 64 is screen size
				uint8_t x0 = (128 - qrcode.size * 2) / 2;
				uint8_t y0 = (64 - qrcode.size * 2) / 2;

				// get QR code pixels in a loop
				for (uint8_t y = 0; y < qrcode.size; y++) {
					for (uint8_t x = 0; x < qrcode.size; x++) {
						// Check this point is black or white
						if (qrcode_getModule(&qrcode, x, y)) {
							u8g2.setColorIndex(1);
						}
						else {
							u8g2.setColorIndex(0);
						}
						// Double the QR code pixels
						u8g2.drawPixel(x0 + x * 2, y0 + y * 2);
						u8g2.drawPixel(x0 + 1 + x * 2, y0 + y * 2);
						u8g2.drawPixel(x0 + x * 2, y0 + 1 + y * 2);
						u8g2.drawPixel(x0 + 1 + x * 2, y0 + 1 + y * 2);
					}
				}

			} while (u8g2.nextPage());
		}

		if (Waiting_Period - 1000 && key == '#') {
			lcd.clear();
			u8g2.clearDisplay();
			Register_step++;
		}
	}
	//產生明文密碼
	else if (Register_step == 4) {
		hmacKeyString = '\0';

		hmacKey[0] = random(65, 91);		 //產生密碼明文
		hmacKey[1] = random(97, 123);
		hmacKey[2] = random(48, 57);
		hmacKey[3] = random(48, 57);
		hmacKey[4] = random(48, 57);

		for (i = 0; i <= 4; i++) {
			hmacKeyString += (char)hmacKey[i];
			hmacKeyArray[i] = (char)hmacKey[i];
		}

		Waiting_Period = millis();
		Register_step++;

	}
	//產生base32密碼與QR code
	else if (Register_step == 5) {
		for (i = 0; i <= 5; i++) { keyIN[i] = '\0';	VerifyCode = '\0'; }
		//base32
		const char* Base32Input = hmacKeyString.c_str();
		strlcpy(rawstr, Base32Input, sizeof(rawstr));	//加密
		encodeBase32(base32, rawstr);
		Serial.print("Base32= ");  Serial.println(base32);

		delay(10);
		Serial.print(F("明文= "));
		Serial.println(hmacKeyString);
		lcd.setCursor(0, 0);
		lcd.print(F(" Scan the QRcode to"));
		lcd.setCursor(0, 1);
		lcd.print(F("  Register Account"));
		lcd.setCursor(0, 2);
		lcd.print(F("    in TOTP APP"));
		lcd.setCursor(0, 3);
		lcd.print(F("Press<#> to Continue"));

		// QR code
		//    otpauth://totp/N?secret=37bvlfgjtw6ylowvzov4qbea6om45cd&issuer=D
		String URL_INPUT;
		URL_INPUT += "otpauth://totp/" + domain;
		URL_INPUT += "?secret=";		  URL_INPUT += base32;
		URL_INPUT += "&issuer=taivs";
		if (address < 100)(URL_INPUT += '0'); if (address < 10) (URL_INPUT += '0');
		URL_INPUT += address;

		const char* URL = URL_INPUT.c_str();

		Serial.println(URL);


		QRCode qrcode;
		uint8_t qrcodeData[qrcode_getBufferSize(3)];
		qrcode_initText(&qrcode, qrcodeData, 3, ECC_LOW, URL);

		// 產生Qrcode
		u8g2.firstPage();
		do {
			// get the draw starting point,128 and 64 is screen size
			uint8_t x0 = (128 - qrcode.size * 2) / 2;
			uint8_t y0 = (64 - qrcode.size * 2) / 2;

			// get QR code pixels in a loop
			for (uint8_t y = 0; y < qrcode.size; y++) {
				for (uint8_t x = 0; x < qrcode.size; x++) {
					// Check this point is black or white
					if (qrcode_getModule(&qrcode, x, y)) {
						u8g2.setColorIndex(1);
					}
					else {
						u8g2.setColorIndex(0);
					}
					// Double the QR code pixels
					u8g2.drawPixel(x0 + x * 2, y0 + y * 2);
					u8g2.drawPixel(x0 + 1 + x * 2, y0 + y * 2);
					u8g2.drawPixel(x0 + x * 2, y0 + 1 + y * 2);
					u8g2.drawPixel(x0 + 1 + x * 2, y0 + 1 + y * 2);
				}
			}

		} while (u8g2.nextPage());
		Register_step++;

	}
	//等待下一步
	else if (Register_step == 6) {
		//key = myKeypad.getKey();
		if (key == '#') {
			lcd.clear();
			i = 0;
			u8g2.clearDisplay();


			Register_step++;
		}
	}
	//輸入&驗證totp密碼
	else if (Register_step == 7) {
		lcd.setCursor(0, 0);
		lcd.print(F("Type taivs"));
		if (address < 100)(lcd.print('0')); if (address < 10) (lcd.print('0')); lcd.print(address);
		lcd.print(F("'s TOTP"));

		lcd.setCursor(0, 2);
		lcd.print(F("Press<*> to Delete"));
		lcd.setCursor(0, 1);
		lcd.print(F("   code: "));
		if (key) {
			lcd.setCursor(9, 1);

			if (key != '*' && key != '#' && i <= 5
				&& key != 'A' && key != 'B' && key != 'C' && key != 'D') {
				keyIN[i] = key;
				i++;
			}
			else if (key == '*' && i > 0) {
				i--;
				keyIN[i] = '\0';
			}
			else if (key == '#' && i == 6) {

				for (i = 0; i <= 5; i++) (VerifyCode += keyIN[i]);
				Serial.print(F("VerifyCode= ")); Serial.println(VerifyCode);
				if (VerifyCode == code || VerifyCode == old_code) {
					lcd.clear();
					u8g2.clearDisplay();
					Register_step++;
				}
				else {
					lcd.clear();
					lcd.setCursor(0, 1);
					lcd.print(F("   Wrong Password"));
					lcd.setCursor(0, 2);
					lcd.print(F("     Try again "));
					delay(2100);
					lcd.clear();
					Register_step -= 2;
				}

			}
			Serial.print(F("KeyIN code= ")); Serial.println(keyIN);
			lcd.print(keyIN);
		}

		if (i != 6) {
			lcd.setCursor(9 + i, 1);
			if (millis() % 1200 <= 670)(lcd.print(F("_")));
			else { lcd.setCursor(9 + i, 1); lcd.print(F(" ")); }
			lcd.print(F("                    "));
			lcd.setCursor(0, 3);
			lcd.print(F("                    "));
		}
		else {
			lcd.setCursor(0, 3);
			lcd.print(F("Press<#> to Continue"));
		}

	}
	//顯示註冊成功畫面
	else if (Register_step == 8) {
		lcd.clear();
		lcd.setCursor(0, 1);			   //註冊成功
		lcd.print(F("  Register Account"));
		lcd.setCursor(0, 2);
		lcd.print(F("    Successfully"));
		Register_step++;
	}
	//寫入帳戶資料,返回待機畫面
	else if (Register_step == 9) {
		for (i = 0; i <= 4; i++) {
			saved_PASSWORD[address][i] = hmacKeyArray[i];		  //注意: 儲存為明文,而非轉換後的base32
			saved_UID[address][i] = tempUID[i];
		}

		Serial.print(F("UID="));
		for (i = 0; i < 4; i++) {
			Serial.print(F(" 0x")); Serial.print(saved_UID[address][i], HEX);
		}
		Serial.println();

		Serial.print(F("PASSWORD= ")); for (i = 0; i <= 4; i++) (Serial.print(saved_PASSWORD[address][i]));
		Serial.println();

		//寫入EEPROM
		//EEPROM.write(address, saved_UID[address][0]);
		//Serial.print("EEPROM寫入="); Serial.println(EEPROM.read(address),HEX);
		//EEPROM.write(address+512, saved_PASSWORD[address]);

		Serial.print(F("address=")); 	Serial.println(address);
		address += 1;                //換下一個寫入位置		
		if (address + 512 == EEPROM.length()) (address = 0); //如果位置達到最後,歸零

		for (i = 0; i <= 4; i++) (hmacKey[i] = '\0');

		Register_step++;
	}

	else if (Register_step == 10) {
		Success = "成功";
		toESP.print("taivs");
		if (address - 1 <= 10)(toESP.print('0'));
		if (address - 1 <= 100)(toESP.print('0'));
		toESP.print(address - 1);
		toESP.print("１");  /////

		toESP.print("0x");			toESP.print(tempUID[0], HEX);
		toESP.print(" 0x");			toESP.print(tempUID[1], HEX);
		toESP.print(" 0x");			toESP.print(tempUID[2], HEX);
		toESP.print(" 0x");			toESP.print(tempUID[3], HEX);
		toESP.print("２");  /////

		toESP.print("註冊");
		toESP.print("３");  /////

		toESP.print(Success);
		toESP.print("４");  /////


		toESP.print('\n');
		toESP.println("");
		delay(4000);
		lcd.clear();
		Register_step = -1;
		step = 0;
	}
	/////////////////////////////登入/////////////////////////////////////
	//讀卡
	if (Login_step == 1) {
		step = -1;
		lcd.setCursor(0, 1);					   //顯示等待訊息
		lcd.print(F("  Waiting to read"));
		lcd.setCursor(0, 2);
		lcd.print(F("   your NFC card"));


		uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 }; //NFC讀取
		uint8_t uidLength;
		uint8_t success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength);

		Serial.print(F("UID Value: "));
		for (uint8_t i = 0; i < 4; i++) {
			tempUID[i] = uid[i];
			Serial.print(F(" 0x")); Serial.print(uid[i], HEX);
		}

		Serial.println();
		lcd.clear();
		byte j;

		//如果未找到卡片,則返回待機畫面
		for (i = 0; i <= 1; i++) {
			for (j = 0; j <= uidLength - 1; j++) {
				if (tempUID[j] == saved_UID[i][j]) {
					done++;
					Serial.println("aa");
					if (done >= 4) {
						Logining = i;
						lcd.clear();
						Login_step++;
						break;
					}
				}
			}
		}

		if (address == 0 || done < 4) {
			lcd.setCursor(0, 1);		 //登入失敗
			lcd.print(F("     Can't find"));
			lcd.setCursor(0, 2);
			lcd.print(F("    Your Account"));
			Success = "失敗 (未註冊帳戶)";

			toESP.print("(未註冊)");

			toESP.print("１");  /////

			toESP.print("0x");			toESP.print(tempUID[0], HEX);
			toESP.print(" 0x");			toESP.print(tempUID[1], HEX);
			toESP.print(" 0x");			toESP.print(tempUID[2], HEX);
			toESP.print(" 0x");			toESP.print(tempUID[3], HEX);
			toESP.print("２");	/////

			toESP.print("註冊");
			toESP.print("３");  /////

			toESP.print(Success);
			toESP.print("４");  /////


			toESP.print('\n');
			toESP.println("");
			delay(3300);
			lcd.clear();
			Login_step = -1;
			step = 0;
		}



		////////////////////////////////////////////////////////////////
		  //如果未找到卡片,則返回待機畫面
  //?
	}
	//確認帳戶,對應TOTP種子
	else if (Login_step == 2) {
		Serial.print(F("Login address="));  Serial.println(Logining);
		Serial.print(F("Login UID="));
		Serial.print(F("UID Value: "));
		for (uint8_t i = 0; i < 4; i++) {
			Serial.print(F(" 0x")); Serial.print(saved_UID[Logining][i], HEX);
		}
		for (i = 0; i <= 4; i++) {
			hmacKey[i] = saved_PASSWORD[Logining][i];
		}
		Serial.println();
		for (i = 0; i <= 4; i++) {
			Serial.print((char)hmacKey[i]);
		}
		Serial.println();
		for (i = 0; i <= 5; i++) { keyIN[i] = '\0';	VerifyCode = '\0'; }
		i = 0;
		Login_step = 3;
	}
	//輸入&驗證密碼
	else if (Login_step == 3) {
		lcd.setCursor(0, 0);
		lcd.print(F("Type taivs"));
		if (Logining < 100)(lcd.print('0')); if (Logining < 10) (lcd.print('0')); lcd.print(Logining);
		lcd.print(F("'s TOTP"));

		lcd.setCursor(0, 2);
		lcd.print(F("Press<*> to Delete"));
		lcd.setCursor(0, 1);
		lcd.print(F("   code: "));
		if (key) {
			lcd.setCursor(9, 1);

			if (key != '*' && key != '#' && i <= 5
				&& key != 'A' && key != 'B' && key != 'C' && key != 'D') {
				keyIN[i] = key;
				i++;
			}
			else if (key == '*' && i > 0) {
				i--;
				keyIN[i] = '\0';
			}
			else if (key == '#' && i == 6) {

				for (i = 0; i <= 5; i++) (VerifyCode += keyIN[i]);
				Serial.print(F("VerifyCode= ")); Serial.println(VerifyCode);
				if (VerifyCode == code || VerifyCode == old_code) {
					lcd.clear();
					u8g2.clearDisplay();
					Login_step++;
				}
				else {
					lcd.clear();
					lcd.setCursor(0, 1);
					lcd.print(F("   Wrong Password"));
					lcd.setCursor(0, 2);
					lcd.print(F("   Back to Home "));

					Success = "失敗 (密碼錯誤)";
					toESP.print("taivs");
					if (Logining <= 10)(toESP.print('0'));
					if (Logining <= 100)(toESP.print('0'));
					toESP.print(Logining);
					toESP.print("１");  /////

					toESP.print("0x");			toESP.print(tempUID[0], HEX);
					toESP.print(" 0x");			toESP.print(tempUID[1], HEX);
					toESP.print(" 0x");			toESP.print(tempUID[2], HEX);
					toESP.print(" 0x");			toESP.print(tempUID[3], HEX);
					toESP.print("２");  /////

					toESP.print("登入");
					toESP.print("３");  /////

					toESP.print(Success);
					toESP.print("４");  /////


					toESP.print('\n');
					toESP.println("");
					delay(2000);
					lcd.clear();
					Login_step = -1;
					step = 0;
				}
			}
			Serial.print(F("KeyIN code= ")); Serial.println(keyIN);
			lcd.print(keyIN);
		}

		if (i != 6) {
			lcd.setCursor(9 + i, 1);
			if (millis() % 1200 <= 670)(lcd.print('_'));
			else { lcd.setCursor(9 + i, 1); lcd.print(" "); }
			lcd.print(F("                    "));
			lcd.setCursor(0, 3);
			lcd.print(F("                    "));
		}
		else {
			lcd.setCursor(0, 3);
			lcd.print(F("Press<#> to Continue"));
		}

	}
	//顯示登入成功畫面,返回待機畫面
	else if (Login_step == 4) {
		Success = "成功";

		toESP.print("taivs");
		if (Logining <= 10)(toESP.print('0'));
		if (Logining <= 100)(toESP.print('0'));
		toESP.print(Logining);
		toESP.print("１");  /////

		toESP.print("0x");			toESP.print(tempUID[0], HEX);
		toESP.print(" 0x");			toESP.print(tempUID[1], HEX);
		toESP.print(" 0x");			toESP.print(tempUID[2], HEX);
		toESP.print(" 0x");			toESP.print(tempUID[3], HEX);
		toESP.print("２");  /////

		toESP.print("登入");
		toESP.print("３");  /////

		toESP.print(Success);
		toESP.print("４");  /////


		toESP.print('\n');
		toESP.println("");

		lcd.clear();
		lcd.setCursor(0, 1);
		lcd.print(F("    Login Account"));
		lcd.setCursor(0, 2);
		lcd.print(F("    Successfully"));
		delay(4000);
		lcd.clear();
		Login_step = -1;
		step = 0;
	}
}
