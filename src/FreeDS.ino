/*
  FreeDS.ino - Main source file
  Derivador de excedentes para ESP32 DEV Kit // Wifi Kit 32

  Based in opends+ (https://github.com/iqas/derivador)
  
  Copyright (C) 2020 Pablo Zerón (https://github.com/pablozg/freeds)

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#define eepromVersion 0x0D

//#define FREEDS_DEBUG
#define FREEDS_INFO

#ifdef FREEDS_INFO
  #define INFO(x) {Serial.print(x); addLog((String)x, false);}
  #define INFOLN(x) {Serial.println(x); addLog((String)x, true);}
#else
  #define INFO(x)
  #define INFOLN(x)
#endif

#ifdef FREEDS_DEBUG
  #define DEBUG(x) {Serial.print(x); addLog((String)x, false);}
  #define DEBUGLN(x) {Serial.println(x); addLog((String)x, true);}
#else
  #define DEBUG(x)
  #define DEBUGLN(x)
#endif

#include <Update.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <EEPROM.h>
#include <TickerScheduler.h>
#include <HardwareSerial.h>

#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncMqttClient.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include <bitmap.h>
#include <DNSServer.h>

extern "C"
{
#include <crypto/base64.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include <rom/rtc.h>
#include <driver/dac.h>
#include <esp_system.h>
}

#define LANGUAGE_ES

#ifdef LANGUAGE_ES
#include "es-ES.h"
#endif

#define OLED

#define MAX_SCREENS 4

#ifdef OLED
    // PWM Pin
  uint8_t pin_pwm = 25;

  // ESP Serial
  uint8_t pin_rx = 17;
  uint8_t pin_tx = 5;

  // RELAY PINS
  #define PIN_RL1 13
  #define PIN_RL2 12
  #define PIN_RL3 14
  #define PIN_RL4 27

  #include "SSD1306.h"
  SSD1306 display(0x3c, 4, 15);
#endif

#ifndef OLED
  // PWM Pin
  uint8_t pin_pwm = 2;

  // ESP Serial
  uint8_t pin_rx = 16;
  uint8_t pin_tx = 17;

  // RELAY PINS
  #define PIN_RL1 5
  #define PIN_RL2 18
  #define PIN_RL3 19
  #define PIN_RL4 21
#endif

// OVERRIDE DEFAULT CONFIG FOR UART1
#define RX1 19
#define TX1 23

//// Temporizadores

struct {
    unsigned long ErrorConexionWifi = 0;
    unsigned long ErrorLecturaDatos = 0;
    unsigned long ErrorConexionRed = 0;
    unsigned long FlashDisplay = 0;
    unsigned long OledAutoOff = 0;
} timers;

///// Debounce control
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 100;
bool ButtonState = false;
bool ButtonLongPress = false;

// Variables Globales
const char compile_date[] PROGMEM = __DATE__ " " __TIME__;
const char version[] PROGMEM = "1.0.4";

const char *www_username = "admin";

uint8_t numeroErrorConexionRemoteApi = 0; // Nº de errores de conexión a remoteapi

uint16_t invert_pwm = 0; // Hasta 1023 con 10 bits resolution
uint16_t last_invert_pwm = 0;

String esp01_version = "";
int esp01_status = 0;
String esp01_payload = "";
int httpcode;

String pro = "";
uint8_t progressbar = 0;

uint8_t Message = 0;

uint8_t screen = 0;
uint8_t workingMode;
uint8_t masterMode = 0;

uint8_t scanDoneCounter = 0;

size_t outputLength; // For base64

// Flags
union {
  uint32_t data;
  struct {
    uint32_t firstInit : 1;       // Bit 0
    uint32_t Updating : 1;        // Bit 1
    uint32_t flash : 1;           // Bit 2
    uint32_t reboot : 1;          // Bit 3
    uint32_t RelayTurnOn : 1;     // Bit 4
    uint32_t RelayTurnOff : 1;    // Bit 5
    uint32_t Relay01Auto : 1;     // Bit 6
    uint32_t Relay02Auto : 1;     // Bit 7
    uint32_t Relay03Auto : 1;     // Bit 8
    uint32_t Relay04Auto : 1;     // Bit 9
    uint32_t Relay01Man : 1;      // Bit 10
    uint32_t Relay02Man : 1;      // Bit 11
    uint32_t Relay03Man : 1;      // Bit 12
    uint32_t Relay04Man : 1;      // Bit 13
    uint32_t setBrightness : 1;   // Bit 14
    uint32_t eventsConnected : 1; // Bit 15
    uint32_t free : 16;           // Bit 16 - 31
  };
} Flags;

//// Flags Errores
union {
  uint8_t data;
  struct {
    uint8_t ConexionWifi : 1;
    uint8_t ConexionInversor : 1;
    uint8_t ConexionMqtt : 1;
    uint8_t LecturaDatos : 1;
    uint8_t RemoteApi : 1;
    uint8_t spare5 : 1;
    uint8_t spare6 : 1;
    uint8_t spare7 : 1;
  };
} Error;

// Estructuras

typedef union {
  uint8_t data;
  struct {
    uint8_t potManPwmActive : 1;
    uint8_t spare1 : 1;
    uint8_t spare2 : 1;
    uint8_t spare3 : 1;
    uint8_t spare4 : 1;
    uint8_t spare5 : 1;
    uint8_t spare6 : 1;
    uint8_t spare7 : 1;
  };
} SysBitfield;

struct CONFIG
{
  byte eeinit;
  uint8_t wversion;
  boolean dhcp;
  boolean P01_on;
  boolean wifi;
  boolean mqtt;

  // STATIC IP
  
  char ip[16];
  char gw[16];
  char mask[16];
  char dns1[16];
  char dns2[16];

  // SALIDAS
  
  int16_t pwm_min;
  int16_t pwm_max;
  boolean R01_man;
  boolean R02_man;
  boolean R03_man;
  boolean R04_man;

  uint16_t R01_min;
  int16_t R01_poton;
  int16_t R01_potoff;

  uint16_t R02_min;
  int16_t R02_poton;
  int16_t R02_potoff;

  uint16_t R03_min;
  int16_t R03_poton;
  int16_t R03_potoff;

  uint16_t R04_min;
  int16_t R04_poton;
  int16_t R04_potoff;

  
  char ssid_esp01[30];
  char password_esp01[30];
  char sensor_ip[30];

  // MQTT
  char MQTT_broker[25];
  char MQTT_user[20];
  char MQTT_password[20];
  uint16_t MQTT_port;
  char R01_mqtt[50];
  char R02_mqtt[50];
  char R03_mqtt[50];
  char R04_mqtt[50];
  char password[30];
  char Solax_mqtt[50];
  char Meter_mqtt[50];

  // WIFI
  char ssid1[30];
  char pass1[30];
  char ssid2[30];
  char pass2[30];
  char hostServer[12];

  // PANTALLA
  boolean oledPower;
  boolean oledAutoOff;
  uint8_t oledBrightness;

  // TEMPORIZADORES
  unsigned long oledControlTime;
  unsigned long pwmControlTime;
  unsigned long maxErrorTime;
  unsigned long getDataTime;

  // PWM
  boolean pwm_man;
  uint8_t manualControlPWM;
  uint8_t autoControlPWM;
  uint16_t pwmFrequency;

  char remote_api[250]; // Por compatibilidad con opends+

  // RESERVAS
  uint16_t baudiosMeter;
  uint8_t idMeter;
  uint8_t pwmSlaveOn;
  uint16_t potmanpwm;
  SysBitfield flags;
  uint8_t free_8;
  uint32_t reserved[2] = {0};
} config;

struct METER
{
  float energyTotal = 0;
  float voltage = 0;
  float current = 0;
  float activePower = 0;
  float aparentPower = 0;
  float reactivePower = 0;
  float powerFactor = 0;
  float frequency = 0;
  float importActive = 0;
  float exportActive = 0;
  float importReactive = 0;
  float exportReactive = 0;
  float phaseAngle = 0;

  uint8_t read_state = 0;
  uint8_t send_retry = 5;
} meter;

struct INVERTER
{
  float pv1c;              // Corriente string 1
  float pv2c;              // Corriente string 2
  float pv1v;              // Tension string 1
  float pv2v;              // Tension string 2
  float pw1;               // Potencia string 1
  float pw2;               // Potencia string 2
  float gridv;             // Tension de red
  float wsolar;            // Potencia solar
  float wtoday;            // Potencia solar diaria
  float wgrid;             // Potencia de red (Negativo: de red - Positivo: a red)
  float wtogrid;           // Potencia enviada a red
  float wgrid_control = 0; // Control del valor de wgrid
} inverter;

struct TIME
{
  long Day = 0;
  int Hour = 0;
  int Minute = 0;
  int Second = 0;
  int HighMillis = 0;
  int Rollover = 0;
} uptime;

struct LOGSTRUCT
{
  String timeStamp;
  String Message;
} Logging[10];
int logcount=-1;

//// Definiciones Conexiones

WiFiClient espClient; // Depurar el motivo de kernel panic sin esta linea
WiFiMulti wifiMulti;

AsyncWebServer server(80);
AsyncEventSource events("/events");
AsyncMqttClient mqttClient;

HardwareSerial SerieEsp(2);   // RX, TX para esp-01
HardwareSerial SerieMeter(1); // RX, TX para los Meter rs485/modbus
DynamicJsonDocument root(2048);

TickerScheduler Tickers(7);

DNSServer dnsServer;

TimerHandle_t mqttReconnectTimer;
TimerHandle_t wifiReconnectTimer;
TimerHandle_t relayOnTimer;
TimerHandle_t relayOffTimer;

//////////////////// CAPTIVE PORTAL ////////////////

String scanNetworks[15];

class CaptiveRequestHandler : public AsyncWebHandler {
public:
  CaptiveRequestHandler() {}
  virtual ~CaptiveRequestHandler() {}

  bool canHandle(AsyncWebServerRequest *request){
    return true;
  }
  
  void handleRequest(AsyncWebServerRequest *request) {
    
    if(request->url() == "/savedata"){
        strcpy(config.ssid1, request->urlDecode(request->arg("wifis")).c_str());
        strcpy(config.pass1, request->urlDecode(request->arg("password")).c_str());
        config.wifi = true;

        EEPROM.put(0, config);
        EEPROM.commit();
        Serial.print("DATA SAVED!!!!, RESTARTING!!!!");
        request->redirect("/");
        delay(500); // Only to be able to redirect client, 
        ESP.restart();

    } else {
      
      AsyncResponseStream *response = request->beginResponseStream("text/html");
      response->print("<!DOCTYPE html><html><head><meta http-equiv='Content-Type' content='text/html; charset=UTF-8'><title>FREEDS</title></head>");
      response->print("<body id='main_body'><div id='form_container'><h1><a>FREEDS</a></h1>");
      response->print("<form id='captive' method='get' action='/savedata'><div class='form_description'><p>1º Seleccione una SSID</p><p>2º Indroduzca el password</p>");
      response->print("<p>3º Pulse Guardar</p><p>4º Espere a que reinicie y acceda a http://" + String(config.hostServer) + ".local/ para una configuración más avanzada.</p></div><ul>");
      response->print("<label class='description' for='wifis'>SSID:</label>");
      response->print("<div><select id='wifis' name='wifis'>");
      response->print("<option disabled selected>Seleccione una red</option>");
      
      for (int i = 0; i < 15; ++i) {
        if (scanNetworks[i] == "") {break;}
        response->print(" <option value='" + scanNetworks[i] + "'>" + scanNetworks[i] + "</option>");
      }
      response->print("</select></div>");
      response->printf("<br><label class='description' for='password'>Password:</label>");
      response->printf("<div><input id='password' name='password' class='element text medium' type='text' maxlength='30' value=''/></div>");
      response->printf("<br><input id='saveForm' class='button_text' type='submit' name='submit' value='Guardar' /></ul>");
      response->printf("</form></div></body></html>");
      request->send(response);
    }
  }
};

////////// WATCHDOG FUNCTIONS //////

const int loopTimeCtl = 0;
hw_timer_t *timer = NULL;

void IRAM_ATTR resetModule()
{
  INFOLN("Reboot by Watchdog");
  ESP.restart();
}

/////////////////////////////////////

void defaultValues()
{
  config.wversion = 2;
  config.mqtt = false;
  strcpy(config.MQTT_broker, "192.168.0.2");
  strcpy(config.MQTT_user, "MQTT_user");
  strcpy(config.MQTT_password, "MQTT_password");
  config.MQTT_port = 1883;
  strcpy(config.ssid1, "MIWIFI1");
  strcpy(config.ssid2, "MIWIFI2");
  strcpy(config.pass1, "DSPLUSWIFI1");
  strcpy(config.pass2, "DSPLUSWIFI2");
  strcpy(config.ssid_esp01, "SOLAXX");
  strcpy(config.password_esp01, "");
  strcpy(config.sensor_ip, "192.168.0.100");
  config.dhcp = true;
  strcpy(config.ip, "192.168.0.99");
  strcpy(config.gw, "192.168.0.1");
  strcpy(config.mask, "255.255.255.0");
  strcpy(config.dns1, "8.8.8.8");
  strcpy(config.dns2, "1.1.1.1");
  strcpy(config.remote_api, "");
  config.pwm_min = -60;
  config.pwm_max = -90;
  config.P01_on = true;
  config.R01_man = false;
  config.R02_man = false;
  config.R03_man = false;
  config.R04_man = false;
  config.R01_min = 999;
  config.R01_poton = 9999;
  config.R01_potoff = 9999;
  config.R02_min = 999;
  config.R02_poton = 9999;
  config.R02_potoff = 9999;
  config.R03_min = 999;
  config.R03_poton = 9999;
  config.R03_potoff = 9999;
  config.R04_min = 999;
  config.R04_poton = 9999;
  config.R04_potoff = 9999;
  strcpy(config.R01_mqtt, "freeds/relay/1/STATUS");
  strcpy(config.R02_mqtt, "freeds/relay/2/STATUS");
  strcpy(config.R03_mqtt, "freeds/relay/3/STATUS");
  strcpy(config.R04_mqtt, "freeds/relay/4/STATUS");
  strcpy(config.Solax_mqtt, "solaxX1/tele/SENSOR");
  strcpy(config.Meter_mqtt, "ddsu666/tele/SENSOR");
  strcpy(config.password, "YWRtaW4=");
  config.oledPower = true;
  config.oledAutoOff = false;
  config.pwmControlTime = 2000;
  config.oledControlTime = 30000;
  config.getDataTime = 1500;
  config.maxErrorTime = 20000;
  config.manualControlPWM = 50;
  config.autoControlPWM = 60;
  config.pwm_man = false;
  config.pwmFrequency = 3000;
  config.oledBrightness = 255;
  config.baudiosMeter = 9600;
  config.idMeter = 1;
  config.pwmSlaveOn = 0;
  config.potmanpwm = 0;
  config.flags.potManPwmActive = false;

  config.eeinit = eepromVersion;
  config.wifi = false;

  byte mac[6];
  WiFi.macAddress(mac);
  sprintf(config.hostServer, "freeds_%02x%02x", mac[4], mac[5]);
}

void setup()
{
  ////////////////////// WATCHDOG //////////////////
  pinMode(loopTimeCtl, INPUT_PULLUP);
  delay(1000);
  timer = timerBegin(0, 240, true); //timer 0, div 240
  timerAttachInterrupt(timer, &resetModule, true);
  timerAlarmWrite(timer, 30000000, false); //set time in us (30 seconds)
  timerAlarmEnable(timer);                 //enable interrupt
  /////////////////////////////////////////////////

  Flags.data = 0; // All Bits to false
  Flags.RelayTurnOn = true;

  Serial.begin(115200); // Se inicia la UART0 para debug
  Serial.setDebugOutput(true);

  INFO("\nCPU0 reset reason: ");
  verbose_print_reset_reason(rtc_get_reset_reason(0));

  INFO("CPU1 reset reason: ");
  verbose_print_reset_reason(rtc_get_reset_reason(1));

  //// Comprobación del estado de la configuración
  INFOLN("\nTesting EEPROM Library\n");
  INFO("EEPROM Size: ");
  INFO(sizeof(config));
  INFOLN(" bytes");

  if (!EEPROM.begin(sizeof(config)))
  {
    INFOLN("Failed to initialise EEPROM");
    INFOLN("Restarting...");
    down_pwm(false);
    delay(1000);
    ESP.restart();
  }

  EEPROM.get(0, config);
  checkEEPROM();

  // OLED
#ifdef OLED
  pinMode(25, OUTPUT);
  pinMode(16, OUTPUT);
  digitalWrite(16, LOW); // set GPIO16 low to reset OLED
  delay(50);
  digitalWrite(16, HIGH); // while OLED is running, must set GPIO16 in high

  display.init();
  display.setBrightness(config.oledBrightness);
  showLogo(_START_, true);
#endif

  if (config.eeinit != eepromVersion)
  {
    INFOLN("Configuration Not Found, initializing");
    showLogo("Config Error", true);
    defaultValues();
    saveEEPROM();
  }

  // Si no está confgurada la wifi, creamos un Punto de acceso con el SSID FreeDS
  if (!config.wifi)
  {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("FreeDS");
    IPAddress myIP = WiFi.softAPIP();
    INFO("Local IP address: ");
    INFOLN(myIP.toString());
    INFOLN("SSID: FreeDS - 192.168.4.1");
    INFO("Hostname: ");
    INFOLN(config.hostServer);
    buildWifiArray();
    Flags.firstInit = true;
    dnsServer.start(53, "*", myIP);
  }
  else
  {

    //// NEW MQTT IMPLEMENTATION
    mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(2000), pdFALSE, (void *)0, reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));
    wifiReconnectTimer = xTimerCreate("wifiTimer", pdMS_TO_TICKS(2000), pdFALSE, (void *)0, reinterpret_cast<TimerCallbackFunction_t>(connectToWifi));
    relayOnTimer = xTimerCreate("relayOnTimer", pdMS_TO_TICKS(5000), pdFALSE, (void *)0, reinterpret_cast<TimerCallbackFunction_t>(enableRelay));
    relayOffTimer = xTimerCreate("relayOffTimer", pdMS_TO_TICKS(5000), pdFALSE, (void *)0, reinterpret_cast<TimerCallbackFunction_t>(disableRelay));

    WiFi.onEvent(WiFiEvent);

    mqttClient.onConnect(onMqttConnect);
    mqttClient.onDisconnect(onMqttDisconnect);
    mqttClient.onMessage(onMqttMessage);
    mqttClient.setClientId(config.hostServer);
    mqttClient.setKeepAlive(30);
    mqttClient.setCredentials(config.MQTT_user, config.MQTT_password);
    mqttClient.setServer(config.MQTT_broker, config.MQTT_port);
    
    connectToWifi();

    if (wifiMulti.run() == WL_CONNECTED) // Si conecta continuamos
    {
      Error.ConexionWifi = false;
     
#ifdef OLED
      showLogo("IP: " + WiFi.localIP().toString(), true);
#endif
      
      buildWifiArray();

      // Configuramos las salidas
      pinMode(PIN_RL1, OUTPUT);
      pinMode(PIN_RL2, OUTPUT);
      pinMode(PIN_RL3, OUTPUT);
      pinMode(PIN_RL4, OUTPUT);
      digitalWrite(PIN_RL1, LOW);
      digitalWrite(PIN_RL2, LOW);
      digitalWrite(PIN_RL3, LOW);
      digitalWrite(PIN_RL4, LOW);
      
      // Serial
      SerieMeter.begin(config.baudiosMeter, SERIAL_8N1, RX1, TX1); // UART1 para meter
      SerieEsp.begin(115200, SERIAL_8N1, pin_rx, pin_tx); // UART2 para ESP01

      INFOLN("Welcome to FreeDS");
      INFO("Hostname: ");
      INFOLN(config.hostServer);

      // Use mdns for host name resolution
      if (!MDNS.begin(config.hostServer))
      {
        INFOLN("Error setting up MDNS responder!");
      }
      else
      {
        INFOLN("mDNS responder started");
      }

      // OUTPUTS
      pinMode(PIN_RL1, OUTPUT);
      pinMode(PIN_RL2, OUTPUT);
      pinMode(PIN_RL3, OUTPUT);
      pinMode(PIN_RL4, OUTPUT);
      // PWM

      // Initialize channels
      ledcSetup(2, config.pwmFrequency, 10); // Frecuencia según configuración, 10-bit resolution
      ledcAttachPin(pin_pwm, 2);
      ledcWrite(2, invert_pwm);

      dac_output_enable(DAC_CHANNEL_2); // Salida Analógica

      if (config.wversion == 2)
        SerieEsp.println("###SSID=" + String(config.ssid_esp01) + "$$$");

      //// Configuración de los tickers
      Tickers.add(0,  200, [&](void *) { data_display(); }, nullptr, true);        // OLED loop
      Tickers.add(1,  500, [&](void *) { send_events(); }, nullptr, false);        // Send Events to Webpage
      Tickers.add(2, 10000, [&](void *) { publishMqtt(); }, nullptr, false);       // Publish Mqtt messages
      Tickers.add(3, 30000, [&](void *) { sendStatusSolaxV2(); }, nullptr, false); // Send status message to ESP01
      Tickers.add(4, 60000, [&](void *) { remote_api(); }, nullptr, false);        // Send Data to Remote API

      Tickers.add(5, config.getDataTime, [&](void *) { getSensorData(); }, nullptr, false); // Sensor data adquisition time
      Tickers.add(6, config.pwmControlTime, [&](void *) { pwmControl(); }, nullptr, true);  // Pwm Control loop

      // Inicialización de Temporizadores
      // timers.ErrorConexionWifi = millis();
      timers.ErrorLecturaDatos = millis();
      timers.ErrorConexionRed = millis();
      timers.OledAutoOff = millis();

      if (config.P01_on && !config.pwm_man)
      {
        workingMode = 0;
      } // AUTO
      else if (config.P01_on && config.pwm_man)
      {
        workingMode = 1;
      } // MANUAL
      else
      {
        workingMode = 2;
      } // OFF

      Error.ConexionInversor = true;
    }
  }

  // http server
  // Initialize SPIFFS
  if (!SPIFFS.begin(true))
  {
    INFOLN("An Error has occurred while mounting SPIFFS");
    return;
  }

  if (!config.wifi) { server.addHandler(new CaptiveRequestHandler).setFilter(ON_AP_FILTER); }
  setWebConfig();
}

void loop()
{
  //////// Watchdog ////////////
  timerWrite(timer, 0); // reset timer (feed watchdog)
  /////////////////////////////////////

  calc_uptime();

  //long tme = millis();

  if (config.wifi && !Flags.firstInit)
  {
    Tickers.update(); // Actualiza todos los tareas Temporizadas
    changeScreen();

    if (Flags.setBrightness) {
      saveEEPROM();
      display.setBrightness((uint8_t)((config.oledBrightness * 255) / 100));
      display.resetDisplay();
      Flags.setBrightness = false;
    }

    if (events.count() == 0) { Flags.eventsConnected = false; }

    if ((millis() - timers.OledAutoOff) > config.oledControlTime)
    {
      if (config.oledAutoOff) {
        config.oledPower = false;
        turnOffOled();
      }
    }

    if ((millis() - timers.ErrorConexionRed) > config.maxErrorTime)
    {
      Error.ConexionInversor = true;
      timers.ErrorConexionRed = millis();
      DEBUGLN("INVERTER ERROR: Error de comunicación");
    }

    if (config.flags.potManPwmActive) {
      if (inverter.wsolar < config.potmanpwm) {
        config.pwm_man = true;
      } else {
        config.pwm_man = false;
      }
    }
  }
  else
  {
    display.clear();
    display.setFont(ArialMT_Plain_10);
    display.drawString(64, 0, _CONNECTSSID_);
    display.drawString(64, 20, "FreeDS");
    display.drawString(64, 40, _CONFIGPAGE_);
    display.display();
    dnsServer.processNextRequest();
  }

  /*INFO("loop time is = ");
  tme = millis() - tme;
  INFOLN(tme);*/
} // End loop
