#include "Arduino.h"

const char* www_username = "ESCAPE.NOW";
char www_password[32];
char wifi_ssid[32];
char wifi_password[32];
char user_data[32];
int user_int, user_stations;

unsigned long lastMillis;

#define MODE_MORSE			1
#define MODE_HTTP			2
#define MODE_SSID			4
int mode;

//#define INPUT0_PIN 			3	//RX //5 //1 //TX
#define OUTPUT0_PIN 		2
//#define LED0_PIN 			2

#ifdef LED0_PIN
bool led;
#endif

#include <DoubleResetDetector.h>
#define DRD_TIMEOUT 10
#define DRD_ADDRESS 0
DoubleResetDetector drd(DRD_TIMEOUT, DRD_ADDRESS);

//#define ESP8266
#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <ESP8266mDNS.h>
#include "ESP8266HTTPClient.h"
#include <ESP8266HTTPUpdateServer.h>
//#include <WiFiManager.h>
#include "WiFiManager.h"

#include <FS.h>
File fsUploadFile;

#define HTTP_AUTH
ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;
#endif

#include <DNSServer.h>
#include <WiFiUdp.h>
//#include <ArduinoOTA.h>
#include <EEPROM.h>

DNSServer dnsServer;
IPAddress deviceIP;

#include "morse.h"

#define FREQUENCY	523
#define NUM_SPEEDS	5
const float wpms[] = {3.0, 5.0, 7.5, 10.0, 13.0, 15.0};
const morseTiming_t durations[] = {34.29, 40.0, 48.0, 60.0, 80.0};
unsigned long lastStartMorseSendingMillis;
LEDMorseSender ledSender(OUTPUT0_PIN, false, wpms[0]);
//LEDMorseSender ledSender(OUTPUT0_PIN, true, wpms[0]);
#ifdef SPEAKER_PIN
SpeakerMorseSender speakerSender(SPEAKER_PIN, FREQUENCY, durations[0]);
#endif
//unsigned int speedIndex = 0;
//unsigned int lastChangeTime;

/*
 ESP8266
 static const uint8_t D0   = 16;
 static const uint8_t D1   = 5;
 static const uint8_t D2   = 4;
 static const uint8_t D3   = 0;
 static const uint8_t D4   = 2;
 static const uint8_t D5   = 14;
 static const uint8_t D6   = 12;
 static const uint8_t D7   = 13;
 static const uint8_t D8   = 15;
 static const uint8_t D9   = 3;
 static const uint8_t D10  = 1;
 */

#if CONFIG_FREERTOS_UNICORE
#define ARDUINO_RUNNING_CORE 0
#else
#define ARDUINO_RUNNING_CORE 1
#endif

//const char* host = "ESCAPE-ESP";
const char* update_path = "/f";

char* htmlHeader =
		"<html><head><title>ESCAPE</title><meta name=\"viewport\" content=\"width=device-width\"><style type=\"text/css\">body{font-family:monospace;} input{padding:5px;font-size:1em;font-family:monospace;} button{height:100px;width:100px;font-family:monospace;border-radius:5px;}</style></head><body><h1><a href=/>ESCAPE</a></h1>";
char* htmlFooter =
		"<hr><a href=/u>FILES</a> <a href=/s>SETTINGS</a></body></html>";

const char* contentType = "text/html";
/*
String getContentType(String filename) { // convert the file extension to the MIME type
	if (filename.endsWith(".html")) return contentType;
	else if (filename.endsWith(".css")) return "text/css";
	else if (filename.endsWith(".js")) return "application/javascript";
	else if (filename.endsWith(".ico")) return "image/x-icon";
	else if (filename.endsWith(".gz")) return "application/x-gzip";
  return contentType;
}
*/
bool handleFileRead(String path) { // send the right file to the client (if it exists)
	//Serial.println("handleFileRead: " + path);
	if (path.endsWith("/")) path += "index.html";          // If a folder is requested, send the index file
		//String contentType = getContentType(path);             // Get the MIME type
		//String pathWithGz = path + ".gz";
		//if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) { // If the file exists, either as a compressed archive, or normal
		//	if (SPIFFS.exists(pathWithGz))                         // If there's a compressed version available
		//		path += ".gz";                                         // Use the compressed verion
		if (SPIFFS.exists(path)) {
			File file = SPIFFS.open(path, "r");                    // Open the file
			size_t sent = httpServer.streamFile(file, contentType);    // Send it to the client
			file.close();                                          // Close the file again
			//Serial.println(String("\tSent file: ") + path);
			return true;
		}
		//Serial.println(String("\tFile Not Found: ") + path);   // If the file doesn't exist, return false
	return false;
}

void handleFileUpload() { // upload a new file to the SPIFFS
	HTTPUpload& upload = httpServer.upload();
	if(upload.status == UPLOAD_FILE_START) {
		String filename = upload.filename;
		if(!filename.startsWith("/"))
			filename = "/" + filename;
		//Serial.print("handleFileUpload Name: ");
		//Serial.println(filename);
		fsUploadFile = SPIFFS.open(filename, "w");            // Open the file for writing in SPIFFS (create if it doesn't exist)
		filename = String();
	}
	else if(upload.status == UPLOAD_FILE_WRITE) {
		if(fsUploadFile)
			fsUploadFile.write(upload.buf, upload.currentSize); // Write the received bytes to the file
		}
	else if(upload.status == UPLOAD_FILE_END) {
		if(fsUploadFile) {                                    // If the file was successfully created
			fsUploadFile.close();                               // Close the file again
			//Serial.print("handleFileUpload Size: ");
			//Serial.println(upload.totalSize);
			httpServer.sendHeader("Location","/u");      // Redirect the client to the success page
			httpServer.send(303);
		}
		else {
			httpServer.send(500, contentType);
		}
	}
}

bool handleFileDelete(String path) {
	if(SPIFFS.exists(path)) {
		return SPIFFS.remove(path);
	}
	return false;
}

void redirectToCaptivePortal() {
	String location = "http://";
	//location += AP_ADDRESS;
	location += wifi_ssid;
	location += "/";

	String message = "<html><head><title>302</title></head><body><a href=\"" + location + "\">CONTINUE HERE</a></body></html>";
	httpServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
	httpServer.sendHeader("Pragma", "no-cache");
	httpServer.sendHeader("Expires", "-1");
	httpServer.sendHeader("Location", location);
	httpServer.send(302, "text/html", message);
}

void saveApi() {
	EEPROM.put(4, mode);

	int offset = 8;
	EEPROM.put(offset, www_password);
	offset += sizeof(www_password);
	EEPROM.put(offset, wifi_ssid);
	offset += sizeof(wifi_ssid);
	EEPROM.put(offset, wifi_password);
	offset += sizeof(wifi_password);
	EEPROM.put(offset, user_data);
	offset += sizeof(user_data);
	EEPROM.put(offset, user_int);
	offset += sizeof(user_int);
	EEPROM.put(offset, user_stations);
	offset += sizeof(user_stations);
	EEPROM.put(0, 0);
	EEPROM.commit();

	ledSender.setMessage(user_data);
#ifdef SPEAKER_PIN
	speakerSender.setMessage(user_data);
#endif
}

/////////////////////////////////
// setup
/////////////////////////////////
void setup() {

	//pinMode(LED_BUILTIN, OUTPUT);
	//digitalWrite(LED_BUILTIN, true);

	//GPIO 1 (TX) swap the pin to a GPIO.
	//pinMode(1, FUNCTION_3);
	//GPIO 3 (RX) swap the pin to a GPIO.
	//pinMode(3, FUNCTION_3);

#ifdef INPUT0_PIN
	pinMode(3, FUNCTION_3);
	pinMode(3, INPUT);
	Serial.begin(9600, SERIAL_8N1, SERIAL_TX_ONLY);
#else
	Serial.begin(9600);
#endif
	Serial.println("\n\nESCAPE");


	if (!SPIFFS.begin()) {
		//Serial.println("SPIFFS Error");
	}
	if (!SPIFFS.exists("/index.html")) {
		//Serial.println("SPIFFS Formating ... ");
		if(SPIFFS.format()) {
			//Serial.println("OK");
		}
		else {
			Serial.println(F("SPIFFS ERRROR"));
		}
	}

	EEPROM.begin(512);
	if (!EEPROM.read(0)) {
		mode = EEPROM.read(4);
		int offset = 8;
		EEPROM.get(offset, www_password);
		offset += sizeof(www_password);
		EEPROM.get(offset, wifi_ssid);
		offset += sizeof(wifi_ssid);
		EEPROM.get(offset, wifi_password);
		offset += sizeof(wifi_password);
		EEPROM.get(offset, user_data);
		offset += sizeof(user_data);
		EEPROM.get(offset, user_int);
		offset += sizeof(user_int);
		EEPROM.get(offset, user_stations);
		offset += sizeof(user_stations);
	}
	else {
		strcpy(www_password, www_username);
		strcpy(wifi_ssid, www_username);
		strcpy(wifi_password, www_username);
		mode = 0;
		strcpy(user_data, "sos");
		user_int = 60;
		user_stations = 0;
		saveApi();
	}

	Serial.println(www_username);
	Serial.println(www_password);

	ledSender.setup();
	ledSender.setMessage(user_data);
#ifdef SPEAKER_PIN
	speakerSender.setup();
	speakerSender.setMessage(user_data);
#endif

	Serial.print(F("M:"));
	Serial.println(mode);

#ifdef INPUT0_PIN
	pinMode(INPUT0_PIN, INPUT);
#endif
#ifdef OUTPUT0_PIN
	digitalWrite(OUTPUT0_PIN, false);
	pinMode(OUTPUT0_PIN, OUTPUT);
#endif

#ifdef LED0_PIN
	pinMode(LED0_PIN, OUTPUT);
	digitalWrite(LED0_PIN, false);
#endif

#ifdef INPUT0_PIN
	if (digitalRead(INPUT0_PIN) == LOW) {

	}
#endif


	if (0 && drd.detectDoubleReset()) {

#ifdef LED0_PIN
		for(int i = 0; i < 16; i++) {
			digitalWrite(LED0_PIN, true);
			delay(125);
			digitalWrite(LED0_PIN, false);
			delay(125);
		}
		digitalWrite(LED0_PIN, false);
#endif
#ifdef LED0_PIN
		digitalWrite(LED0_PIN, true);
#endif

		strcpy(www_password, www_username);
		mode = 0;


		Serial.println(F("CONN AP"));

		WiFiManager wifiManager;
		//if (wifiManager.autoConnect()) {
		if (wifiManager.connectWifi("", "") == WL_CONNECTED) {
			Serial.print(F("IP:"));
			Serial.println(WiFi.localIP());
		}
		else {
			//WiFi.disconnect()
			//wifiManager.resetSettings();
			wifiManager.startConfigPortal(wifi_ssid);
			//wifiManager.startConfigPortal();
		}
	}
	else {
		Serial.println(F("START AP"));
		Serial.println(wifi_ssid);
		Serial.println(wifi_password);

		deviceIP = IPAddress(192, 168, 4, 1);
		WiFi.mode(WIFI_AP);
		WiFi.softAPConfig(deviceIP, deviceIP, IPAddress(0, 0, 0, 0));
		//WiFi.softAPConfig(deviceIP, deviceIP, IPAddress(255, 255, 255, 0));

		if(wifi_password[0])
			WiFi.softAP(wifi_ssid, wifi_password);
		else
			WiFi.softAP(wifi_ssid);

		dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
		dnsServer.start(53, "*", deviceIP);

		deviceIP = WiFi.softAPIP();
		Serial.print("AP IP:");
		Serial.println(deviceIP);
	}

	Serial.println(F("READY"));
	WiFi.printDiag(Serial);

	httpServer.onNotFound(redirectToCaptivePortal);

	httpServer.on("/", []() {
		Serial.println("/");
#ifdef HTTP_AUTH
		//if(!httpServer.authenticate(www_username, www_password))
		//return httpServer.requestAuthentication();
#endif

		if(mode != MODE_MORSE) {
			if(WiFi.softAPgetStationNum() < user_stations) {
				String message = String(WiFi.softAPgetStationNum());
				message += "/";
				message += user_stations;
				httpServer.send(200, contentType, message);
				return;
			}

			if (!handleFileRead("/index.html"))  {
				//String message = htmlHeader;
				String message = user_data;
				//message += htmlFooter;
				httpServer.send(200, contentType, message);
			}
		}

		//else {
		//	httpServer.send(404);
		//}
	});

	httpServer.on("/u", HTTP_GET, []() {
		//Serial.println("/u GET");
#ifdef HTTP_AUTH
		if(!httpServer.authenticate(www_username, www_password))
			return httpServer.requestAuthentication();
#endif
		String message = htmlHeader;
		message += "<h2>FILES</h2><hr>";
		message += "<form method=\"post\" enctype=\"multipart/form-data\"><input type=\"file\" name=\"name\"><br><button type=\"submit\">UPLOAD</button></form>";
		message += "<br>";
		Dir dir = SPIFFS.openDir("/");
		while (dir.next()) {
		    //Serial.print(dir.fileName());
		    if(dir.fileSize()) {
		        File file = dir.openFile("r");
		        //Serial.println(file.size());

				message += "<a href=";
				message += file.name();
				message += ">";
				message += file.name();
				message += "</a>";

				message += " <a href=/d?f=";
				message += file.name();
				message += ">DELETE</a></br>";

				//Serial.print("File: ");
				//Serial.println(file.name());
		    }
		}

		message += htmlFooter;
		httpServer.send(200, contentType, message);

		//if (!handleFileRead("/upload.html"))                // send it if it exists
		//	httpServer.send(404, contentType, "404: Not Found"); // otherwise, respond with a 404 (Not Found) error
	});

	httpServer.on("/u", HTTP_POST, []() {
		Serial.println("/u POST");
#ifdef HTTP_AUTH
		if(!httpServer.authenticate(www_username, www_password))
			return httpServer.requestAuthentication();
#endif
		httpServer.send(200); // Send status 200 (OK) to tell the client we are ready to receive
		},
		handleFileUpload
	);

	httpServer.on("/d", HTTP_GET, []() {
#ifdef HTTP_AUTH
		if(!httpServer.authenticate(www_username, www_password))
			return httpServer.requestAuthentication();
#endif
		if (httpServer.args() > 0 ) { // Arguments were received
			if (httpServer.hasArg("f")) {
				String message = htmlHeader;
				message += "OK";
				message += htmlFooter;
				httpServer.send(200, contentType, message);
				handleFileDelete(httpServer.arg(0));
			}
		}
		httpServer.send(404);
	});

	httpServer.onNotFound([]() {
		if (!handleFileRead(httpServer.uri()))  {
			const char *metaRefreshStr = "<htm><head><meta http-equiv=\"refresh\" content=\"0; url=http://192.168.4.1/\" /></head></html>";
			httpServer.send(200, contentType, metaRefreshStr);
		}
	});

	httpServer.on("/s", []() {
	//Serial.println("/s");

#ifdef HTTP_AUTH
		if(!httpServer.authenticate(www_username, www_password))
			return httpServer.requestAuthentication();
#endif

		String message = htmlHeader;
		message += "<h2>SETTINGS</h2><hr>";

		message += "<form action=/c>";

#ifdef HTTP_AUTH
		message += "ADMIN PASSWORD<br><input name=www_p value=";
		message += www_password;
		message += "><hr>";
#endif
		message += "WIFI SSID<br><input name=wifi_s value=";
		message += wifi_ssid;
		message += "><br><br>";
		message += "WIFI PASSWORD<br><input name=wifi_p value=";
		message += wifi_password;
		message += "><hr>";

		message += "WIFI USERS<br><input name=u_s value=";
		message += user_stations;
		message += "><hr>";

		message += "MORSE TEXT<br><input name=u_d value=\"";

		String htmlString = user_data;
		htmlString.replace("&", "&amp;");
		htmlString.replace("<", "&lt;");
		htmlString.replace(">", "&gt;");
		message += htmlString;

		message += "\"><br><br>";
		message += "INTERVAL<br><input name=u_i value=";
		message += user_int;
		message += "><hr>";

		message += "MODE<br><input name=m value=";
		message += mode;
		message += "><h3>";
		message += "MODE: ";
		message += mode;
		message += "</h3>";

		message += "0: MORSE HTTP<br>";
		message += "1: MORSE<br>";
		message += "2: HTTP<br>";
		message += "4: WIFI SSID CHANGE<br>";
		message += "<hr>";


		message += "<button type=submit name=cmd value=c>SET!</button>";
		message += "</form>";
		message += htmlFooter;
		httpServer.send(200, contentType, message);
	});

	httpServer.on("/c", []() {
		//Serial.println("/c");

#ifdef HTTP_AUTH
		if(!httpServer.authenticate(www_username, www_password))
		return httpServer.requestAuthentication();
#endif

		String message = htmlHeader;

		if(httpServer.arg("cmd").equals("c")) {
			//TODO: disable for demo
			strcpy(www_password, httpServer.arg("www_p").c_str());
			strcpy(wifi_ssid, httpServer.arg("wifi_s").c_str());
			strcpy(wifi_password, httpServer.arg("wifi_p").c_str());
			strcpy(user_data, httpServer.arg("u_d").c_str());
			user_int = httpServer.arg("u_i").toInt();
			user_stations = httpServer.arg("u_s").toInt();
			mode = httpServer.arg("m").toInt();
			message += "SET";
			saveApi();
		}

		message += htmlFooter;
		httpServer.send(200, contentType, message);
	});

#ifndef ESP8266
	//const char* serverIndex = "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>";
	httpServer.on("/update", HTTP_GET, [](){
        if(!httpServer.authenticate(www_username, www_password))
        	return httpServer.requestAuthentication();
        httpServer.sendHeader("Connection", "close");
        String message = htmlHeader;
		message += serverIndex;
		message += htmlFooter;
		httpServer.send(200, contentType, message);
	});

	httpServer.on("/update", HTTP_POST, [](){
        if(!httpServer.authenticate(www_username, www_password))
            return httpServer.requestAuthentication();
        httpServer.sendHeader("Connection", "close");\
        httpServer.send(200, contentType, (Update.hasError())?"FAIL":"OK");
        ESP.restart();
      },[](){
    	  HTTPUpload& upload = httpServer.upload();
    	  if(upload.status == UPLOAD_FILE_START){
    		  Serial.setDebugOutput(true);
    		  Serial.printf("Update: %s\n", upload.filename.c_str());
#ifdef ESP8266
    		  uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
#else
    		  uint32_t maxSketchSpace = 0x140000;//(ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
#endif
    		  if(!Update.begin(maxSketchSpace)){//start with max available size
    			  Update.printError(Serial);
    		  }
    	  }
    	  else if(upload.status == UPLOAD_FILE_WRITE){
    		  if(Update.write(upload.buf, upload.currentSize) != upload.currentSize){
    			  Update.printError(Serial);
    		  }
    	  } else if(upload.status == UPLOAD_FILE_END){
    		  if(Update.end(true)){ //true to set the size to the current progress
    			  Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
    		  }
    		  else {
    			  Update.printError(Serial);
    		  }
    		  Serial.setDebugOutput(false);
    	  }
    	  yield();
      });
#endif

	httpServer.begin();
	//MDNS.begin(host);

#ifdef ESP8266
	httpUpdater.setup(&httpServer, update_path);//, www_username, www_password);
#endif

	/*
	//MDNS.addService("http", "tcp", 80);
	//ArduinoOTA.setPort(8266);
	ArduinoOTA.setHostname(host); //8266
	//ArduinoOTA.setPassword((const char *)"xxxxx");
	ArduinoOTA.onStart([]() {
		Serial.println("OTA Start");
	});
	ArduinoOTA.onEnd([]() {
		Serial.println("OTA End");
		Serial.println("Rebooting...");
	});
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
		Serial.printf("Progress: %u%%\r\n", (progress / (total / 100)));
	});
	ArduinoOTA.onError([](ota_error_t error) {
		Serial.printf("Error[%u]: ", error);
		if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
		else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
		else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
		else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
		else if (error == OTA_END_ERROR) Serial.println("End Failed");
	});
	ArduinoOTA.begin();
	*/

#ifndef ESP8266
	xTaskCreatePinnedToCore(loopComm, "loopComm", 4096, NULL, 1, NULL, ARDUINO_RUNNING_CORE);
#endif
}

#ifndef ESP8266
void drawNextFrame(OLEDDisplay *display) {
	frameNo++;
	drawDisplay(display, frameNo);
}
#endif

/////////////////////////////////////
// loop
/////////////////////////////////////
int iWiFiSSID = 0;
uint8_t iWiFiSSIDUserStations = 0;
unsigned long lastWiFiSSIDMillis;
char lastWiFiSSID[32];

void loop() {

	if(max(iWiFiSSIDUserStations, WiFi.softAPgetStationNum()) != iWiFiSSIDUserStations) {
		lastWiFiSSIDMillis = 0;
		iWiFiSSIDUserStations = max(iWiFiSSIDUserStations, WiFi.softAPgetStationNum());
	}

	if(mode < 2) {
		if(!ledSender.continueSending()) {
			if(lastStartMorseSendingMillis == 0 || millis() - lastStartMorseSendingMillis > (unsigned long)user_int * 1000) {
				lastStartMorseSendingMillis = millis();
				ledSender.startSending();
#ifdef SPEAKER_PIN
				speakerSender.startSending();
#endif
			}
			//if((millis() - lastChangeTime) > 5000) {
			//	speedIndex++;
			//	lastChangeTime = millis();
			//	ledSender.setWPM(wpms[speedIndex % NUM_SPEEDS]);
			//	speakerSender.setSpeed(durations[speedIndex % NUM_SPEEDS]);
			//}
		}
	}
	else if(mode >= 2) {
		//digitalWrite(OUTPUT0_PIN, true);
		if(millis() - lastStartMorseSendingMillis > (unsigned long)user_int / (WiFi.softAPgetStationNum() + 1)) {
			digitalWrite(OUTPUT0_PIN, !digitalRead(OUTPUT0_PIN));
			lastStartMorseSendingMillis = millis();
		}
	}

	if(mode == 4) {
		if(millis() - lastWiFiSSIDMillis > max((unsigned long)user_int, 20000UL)) {
			lastWiFiSSIDMillis = millis();


			//WiFi.softAPgetStationNum() < userStations
			if(user_stations)
				iWiFiSSID = iWiFiSSIDUserStations;
			else
				iWiFiSSID++;

			char s[32];
			strncpy(s, user_data, sizeof(user_data));
			String tempWiFiSSID = String(s);
			char *pch, *lastpch;
			pch = strtok (s," ");
			lastpch = pch;
			int i = 1;
			while (pch != NULL && i < iWiFiSSID) {
			    i++;
			    lastpch = pch;
			    pch = strtok (NULL, " ");
			}
			if(pch == NULL) {
				iWiFiSSID = 0;
				if(user_stations && lastpch)
					pch = lastpch;
				else
					pch = wifi_ssid;
			}

			strncpy(s, pch, 32);
			for(i = strlen(s); i < 31; i++) {
				s[i] = ' ';
			}
			s[31] = 0;

			if(strncmp(lastWiFiSSID, s, 32)) {
				strncpy(lastWiFiSSID, s, sizeof(user_data));
				Serial.println(s);
				//esp_wifi_deinit();
				//esp_wifi_stop();
				WiFi.disconnect();
				//WiFi.mode(WIFI_OFF);
				if(wifi_password[0])
					WiFi.softAP(s, wifi_password);
				else
					WiFi.softAP(s);
			}
		}
	}



	drd.loop();

	// WDG test
	//ESP.wdtDisable(); // disable sw wdt, hw wdt keeps on
	//while(1) {};

	//ArduinoOTA.handle();
	dnsServer.processNextRequest();
	httpServer.handleClient();

#ifdef INPUT0_PIN

#endif

	if (millis() - lastMillis >= 1000) {
		lastMillis = millis();
		//Serial.println(millis());

#ifdef LED0_PIN
		digitalWrite(LED0_PIN, led);
		led = !led;
#endif

	}

#ifdef OUTPUT0_PIN

#endif
}
