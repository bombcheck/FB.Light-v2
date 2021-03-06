// FB.Light Responsive LED Control
// https://github.com/bombcheck/FB.Light
//
// Forked from doctormord's Responsive Led Control
// https://github.com/doctormord/Responsive_LED_Control
//
// Free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as 
// published by the Free Software Foundation, either version 3 of 
// the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.


// ***************************************************************************
// Load libraries for: WebServer / WiFiManager / WebSockets
// ***************************************************************************
#include <NTPClient.h>
#include <ESP8266WiFi.h>  //https://github.com/esp8266/Arduino
#include <WiFiUdp.h>

// needed for library WiFiManager
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>  //https://github.com/tzapu/WiFiManager

#include <ESP8266mDNS.h>
#include <FS.h>
#include <WiFiClient.h>

#include <Ticker.h>

#include <WebSockets.h>  //https://github.com/Links2004/arduinoWebSockets
#include <WebSocketsServer.h>

#ifdef ENABLE_OTA
  #include <ArduinoOTA.h>
#endif

// ***************************************************************************
// Sub-modules of this application
// ***************************************************************************
#include "definitions.h"
#include "eepromsettings.h"
#include "colormodes.h"
#include "scrollingtext.h"

// ***************************************************************************
// Instanciate HTTP(80) / WebSockets(81) Server
// ***************************************************************************
ESP8266WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

#ifdef HTTP_OTA
  #include <ESP8266HTTPUpdateServer.h>
  ESP8266HTTPUpdateServer httpUpdater;
#endif

// ***************************************************************************
// Load library "ticker" for blinking status led
// ***************************************************************************
Ticker ticker;

void tick() {
  // toggle state
  int state = digitalRead(BUILTIN_LED);  // get the current state of GPIO1 pin
  digitalWrite(BUILTIN_LED, !state);     // set pin to the opposite state
}

// ***************************************************************************
// Callback for WiFiManager library when config mode is entered
// ***************************************************************************
// gets called when WiFiManager enters configuration mode
void configModeCallback(WiFiManager *myWiFiManager) {
  DBG_OUTPUT_PORT.println("Entered config mode");
  DBG_OUTPUT_PORT.println(WiFi.softAPIP());
  // if you used auto generated SSID, print it
  DBG_OUTPUT_PORT.println(myWiFiManager->getConfigPortalSSID());
  // entered config mode, make led toggle faster
  ticker.attach(0.2, tick);
  
  // Show USER that module can't connect to stored WiFi
  uint16_t i;
  for (i = 0; i < NUM_LEDS; i++) {
    leds(i).setRGB(0, 0, 50);
  }
  FastLED.show(); 
}

// ***************************************************************************
// Include: Webserver & Request Handlers
// ***************************************************************************
#include "spiffs_webserver.h"    // must be included after the 'server' object
#include "request_handlers.h"    // is declared.

// ***************************************************************************
// MAIN
// ***************************************************************************
void setup() {
  
  // Generate a pseduo-unique hostname
  char hostname[strlen(HOSTNAME_PREFIX)+6];
  uint16_t chipid = ESP.getChipId() & 0xFFFF;
  sprintf(hostname, "%s-%04x",HOSTNAME_PREFIX, chipid);

  wifi_station_set_hostname(const_cast<char*>(hostname));
  
#ifdef REMOTE_DEBUG
  Debug.begin(hostname);  // Initiaze the telnet server - hostname is the used
                          // in MDNS.begin
  Debug.setResetCmdEnabled(true);  // Enable the reset command
#endif

  // ***************************************************************************
  // Setup: EEPROM
  // ***************************************************************************
  initSettings();  // setting loaded from EEPROM or defaults if fail
  printSettings();

  ///*** Random Seed***
  randomSeed(analogRead(0));

  #ifndef REMOTE_DEBUG
    DBG_OUTPUT_PORT.begin(115200);
  #endif
  DBG_OUTPUT_PORT.printf("system_get_cpu_freq: %d\n", system_get_cpu_freq());

  // set builtin led pin as output
  pinMode(BUILTIN_LED, OUTPUT);
  // start ticker with 0.5 because we start in AP mode and try to connect
  ticker.attach(0.5, tick);

 // ***************************************************************************
  // Setup: FASTLED
  // ***************************************************************************
  delay(500);  // 500ms delay for recovery

  // limit my draw to 3A at 5v of power draw
  FastLED.setMaxPowerInVoltsAndMilliamps(5,MAX_CURRENT);

  // maximum refresh rate
  FastLED.setMaxRefreshRate(FASTLED_HZ);

  // tell FastLED about the LED strip configuration
  #ifdef CLK_PIN
    FastLED.addLeds<LED_TYPE, DATA_PIN, CLK_PIN, COLOR_ORDER>(leds[0], leds.Size())
      .setCorrection(TypicalLEDStrip);
  #else
    FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds[0], leds.Size())
      .setCorrection(TypicalLEDStrip);
  #endif

  // set master brightness control
  FastLED.setBrightness(settings.overall_brightness);

  // Initialize stuff for scrolling text
  for (int i=0; i < TEXT_DATA_PREFIX_COUNT; i++) TextDataPrefix += " ";
  ScrollingMsg.SetFont(MatriseFontData);
  ScrollingMsg.Init(&leds, leds.Width(), leds.Height(), 0, 0);
  ScrollingMsg.SetScrollDirection(SCROLL_LEFT);

  // ***************************************************************************
  // Setup: WiFiManager
  // ***************************************************************************
  // Local intialization. Once its business is done, there is no need to keep it
  // around
  WiFiManager wifiManager;
  // reset settings - for testing
  // wifiManager.resetSettings();

  // set callback that gets called when connecting to previous WiFi fails, and
  // enters Access Point mode
  wifiManager.setAPCallback(configModeCallback);

  // fetches ssid and pass and tries to connect
  // if it does not connect it starts an access point with the specified name
  // here  "AutoConnectAP"
  // and goes into a blocking loop awaiting configuration
  
  if (!wifiManager.autoConnect(hostname)) {
    DBG_OUTPUT_PORT.println("failed to connect and hit timeout");
    // reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(1000);
  }

  // if you get here you have connected to the WiFi
  DBG_OUTPUT_PORT.println("connected...yeey :)");
  ticker.detach();
  // keep LED on
  digitalWrite(BUILTIN_LED, LOW);

#ifdef ENABLE_OTA
  // ***************************************************************************
  // Setup: ArduinoOTA
  // ***************************************************************************
  ArduinoOTA.setHostname(hostname);
  ArduinoOTA.onStart([]() {

    String type;
//    if (ArduinoOTA.getCommand() == U_FLASH)
//      type = "sketch";
//    else
//      type = "filesystem";
//      
    SPIFFS.end(); // unmount SPIFFS for update.
    // DBG_OUTPUT_PORT.println("Start updating " + type);
    DBG_OUTPUT_PORT.println("Start updating ");
  });
  ArduinoOTA.onEnd([]() { 
    DBG_OUTPUT_PORT.println("\nEnd... remounting SPIFFS");
    SPIFFS.begin();
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    DBG_OUTPUT_PORT.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    DBG_OUTPUT_PORT.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
      DBG_OUTPUT_PORT.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR)
      DBG_OUTPUT_PORT.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR)
      DBG_OUTPUT_PORT.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR)
      DBG_OUTPUT_PORT.println("Receive Failed");
    else if (error == OTA_END_ERROR)
      DBG_OUTPUT_PORT.println("End Failed");
  });

  ArduinoOTA.begin();
  DBG_OUTPUT_PORT.println("OTA Ready");
#endif

  DBG_OUTPUT_PORT.print("IP address: ");
  DBG_OUTPUT_PORT.println(WiFi.localIP());
 
  // ***************************************************************************
  // Setup: MDNS responder
  // ***************************************************************************
  MDNS.begin(hostname);
  DBG_OUTPUT_PORT.print("Open http://");
  DBG_OUTPUT_PORT.print(hostname);
  DBG_OUTPUT_PORT.println(".local/upload to upload files");
  DBG_OUTPUT_PORT.println("Use /edit for file browser");

  // ***************************************************************************
  // Setup: WebSocket server
  // ***************************************************************************
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  // ***************************************************************************
  // Setup: SPIFFS
  // ***************************************************************************
  SPIFFS.begin();
  {
    Dir dir = SPIFFS.openDir("/");
    while (dir.next()) {
      String fileName = dir.fileName();
      size_t fileSize = dir.fileSize();
      DBG_OUTPUT_PORT.printf("FS File: %s, size: %s\n", fileName.c_str(),
                             formatBytes(fileSize).c_str());
    }
    DBG_OUTPUT_PORT.printf("\n");
  }

  // ***************************************************************************
  // Setup: NTPclient
  // ***************************************************************************

  timeClient.begin();

  // ***************************************************************************
  // Setup: SPIFFS Webserver handler
  // ***************************************************************************

  // list directory
  server.on("/list", HTTP_GET, handleFileList);
  
  // load editor
  server.on("/edit", HTTP_GET, []() {
    if (!handleFileRead("/edit.htm"))
      server.send(404, "text/plain", "FileNotFound");
  });
  
  // create file
  server.on("/edit", HTTP_PUT, handleFileCreate);
  
  // delete file
  server.on("/edit", HTTP_DELETE, handleFileDelete);
  
  // first callback is called after the request has ended with all parsed
  // arguments
  // second callback handles file uploads at that location
  server.on("/edit", HTTP_POST, []() { server.send(200, "text/plain", ""); },
            handleFileUpload);
  
  // get heap status, analog input value and all GPIO statuses in one json call
  server.on("/esp_status", HTTP_GET, []() {
    String json = "{";
    json += "\"heap\":" + String(ESP.getFreeHeap());
    json += ", \"analog\":" + String(analogRead(A0));
    json += ", \"gpio\":" +
            String((uint32_t)(((GPI | GPO) & 0xFFFF) | ((GP16I & 0x01) << 16)));
    json += "}";
    server.send(200, "application/json", json);
    json = String();
  });

  // called when the url is not defined here
  // use it to load content from SPIFFS
  server.onNotFound([]() {
    if (!handleFileRead(server.uri())) handleNotFound();
  });

  server.on("/upload", handleMinimalUpload);

  server.on("/restart", []() {
    DBG_OUTPUT_PORT.printf("/restart:\n");
    server.send(200, "text/plain", "restarting...");
    ESP.restart();
  });

  server.on("/reset_wlan", []() {
    DBG_OUTPUT_PORT.printf("/reset_wlan:\n");
    server.send(200, "text/plain", "Resetting WLAN and restarting...");
    WiFiManager wifiManager;
    wifiManager.resetSettings();
    ESP.restart();
  });

  // ***************************************************************************
  // Setup: SPIFFS Webserver handler
  // ***************************************************************************
  server.on("/set_brightness", []() {
    if (server.arg("c").toInt() > 0) {
      settings.overall_brightness = (int)server.arg("c").toInt() * 2.55;
    } else {
      settings.overall_brightness = server.arg("p").toInt();
    }
    if (settings.overall_brightness > 255) {
      settings.overall_brightness = 255;
    }
    if (settings.overall_brightness < 0) {
      settings.overall_brightness = 0;
    }
    FastLED.setBrightness(settings.overall_brightness);

    if (settings.mode == HOLD) {
      settings.mode = ALL;
    }

    getStatusJSON();
  });

  server.on("/set_clock_brightness", []() {
    if (server.arg("c").toInt() > 0) {
      settings.clock_brightness = (int)server.arg("c").toInt() * 2.55;
    } else {
      settings.clock_brightness = server.arg("p").toInt();
    }
    if (settings.clock_brightness > 255) {
      settings.clock_brightness = 255;
    }
    if (settings.clock_brightness < 0) {
      settings.clock_brightness = 0;
    }

    getStatusJSON();
  });

  server.on("/set_text_brightness", []() {
    if (server.arg("c").toInt() > 0) {
      settings.text_brightness = (int)server.arg("c").toInt() * 2.55;
    } else {
      settings.text_brightness = server.arg("p").toInt();
    }
    if (settings.text_brightness > 255) {
      settings.text_brightness = 255;
    }
    if (settings.text_brightness < 0) {
      settings.text_brightness = 0;
    }

    getStatusJSON();
  });

  server.on("/set_clock", []() {
    if (server.arg("s").toInt() ==  1) {
      settings.show_clock = true;
      clockAppearTimer = 0;
    } 
    else {
      settings.show_clock = false;
      clockAppearTimer = 0;
    } 

    getStatusJSON();
  });

  server.on("/set_text", []() {
    if (server.arg("s").toInt() ==  1) {
      settings.show_text = true;
      textAppearTimer = 0;
    } 
    else {
      settings.show_text = false;
      textAppearTimer = 0;
    } 

    getStatusJSON();
  });

  server.on("/update_text", []() {
    if (server.arg("text") ==  "") {
    server.send(200, "text/plain", "Error: Paramter 'text' is missing!");
    } 
    else {
      if (server.arg("color") !=  "") {
        settings.text_color = server.arg("color").toInt();
        if (settings.text_color > 6) {
          settings.text_color = 6;
        }
        if (settings.text_color < 0) {
          settings.text_color = 0;
        }
      }
      
      if (server.arg("text").length() > 255) {
        server.send(200, "text/plain", "Error: Text can not have more than 255 characters!"); 
      }
      else {
        server.arg("text").toCharArray(settings.text_msg,server.arg("text").length() + 1);
        settings.text_length = server.arg("text").length();
        textAppearTimer = 0;
        if (settings.show_text == false || settings.mode == OFF) TextLoaded = true;
        server.send(200, "text/plain", "OK: Loaded scrolling text '" + server.arg("text") + "' with color " + settings.text_color + ".");
      }
   } 
  });

  server.on("/get_brightness", []() {
    String str_brightness = String((int)(settings.overall_brightness / 2.55));
    server.send(200, "text/plain", str_brightness);
    DBG_OUTPUT_PORT.print("/get_brightness: ");
    DBG_OUTPUT_PORT.println(str_brightness);
  });

  server.on("/get_clock_brightness", []() {
    String str_clock_brightness = String((int)(settings.clock_brightness / 2.55));
    server.send(200, "text/plain", str_clock_brightness);
    DBG_OUTPUT_PORT.print("/get_clock_brightness: ");
    DBG_OUTPUT_PORT.println(str_clock_brightness);
  });

  server.on("/get_text_brightness", []() {
    String str_text_brightness = String((int)(settings.text_brightness / 2.55));
    server.send(200, "text/plain", str_text_brightness);
    DBG_OUTPUT_PORT.print("/get_text_brightness: ");
    DBG_OUTPUT_PORT.println(str_text_brightness);
  });

  server.on("/get_switch", []() {
    server.send(200, "text/plain", (settings.mode == OFF) ? "0" : "1");
    DBG_OUTPUT_PORT.printf("/get_switch: %s\n",
                           (settings.mode == OFF) ? "0" : "1");
  });

  server.on("/get_color", []() {
    String rgbcolor = String(settings.main_color.red, HEX) +
                      String(settings.main_color.green, HEX) +
                      String(settings.main_color.blue, HEX);
    server.send(200, "text/plain", rgbcolor);
    DBG_OUTPUT_PORT.print("/get_color: ");
    DBG_OUTPUT_PORT.println(rgbcolor);
  });

  server.on("/restore_defaults", []() {
    loadDefaults();
    getStatusJSON();
  });

  server.on("/save_settings", []() {
    saveSettings();
    server.send(200, "text/plain", "Current settings saved!");
  });

  server.on("/load_settings", []() {
    readSettings(false);
    getStatusJSON();
  });

  server.on("/status", []() { getStatusJSON(); });

  server.on("/off", []() {
    //exit_func = true;
    settings.mode = OFF;
    getArgs();
    getStatusJSON();
  });

  server.on("/blank", []() {
    //exit_func = true;
    settings.mode = BLANK;
    getArgs();
    getStatusJSON();
  });

  server.on("/all", []() {
    //exit_func = true;
    settings.mode = ALL;
    getArgs();
    getStatusJSON();
  });

  server.on("/rainbow", []() {
    //exit_func = true;
    settings.mode = RAINBOW;
    getArgs();
    getStatusJSON();
  });

  server.on("/confetti", []() {
    //exit_func = true;
    settings.mode = CONFETTI;
    getArgs();
    getStatusJSON();
  });

  server.on("/sinelon", []() {
    //exit_func = true;
    settings.mode = SINELON;
    getArgs();
    getStatusJSON();
  });

  server.on("/juggle", []() {
    //exit_func = true;
    settings.mode = JUGGLE;
    getArgs();
    getStatusJSON();
  });

  server.on("/bpm", []() {
    //exit_func = true;
    settings.mode = BPM;
    getArgs();
    getStatusJSON();
  });

  server.on("/ripple", []() {
    //exit_func = true;
    settings.mode = RIPPLE;
    getArgs();
    getStatusJSON();
  });

  server.on("/comet", []() {
    //exit_func = true;
    settings.mode = COMET;
    getArgs();
    getStatusJSON();
  });

  server.on("/wipe", []() {
    settings.mode = WIPE;
    getArgs();
    getStatusJSON();
  });

  server.on("/tv", []() {    
    settings.mode = TV;
    getArgs();
    getStatusJSON();
  });

server.on("/fire", []() {
    //exit_func = true;
    settings.mode = FIRE;
    getArgs();
    getStatusJSON();
  });

  server.on("/frainbow", []() {
    //exit_func = true;
    settings.mode = FIRE_RAINBOW;
    getArgs();
    getStatusJSON();
  });

  server.on("/fworks", []() {
    //exit_func = true;
    settings.mode = FIREWORKS;
    getArgs();
    getStatusJSON();
  });

  server.on("/fwsingle", []() {
    //exit_func = true;
    settings.mode = FIREWORKS_SINGLE;
    getArgs();
    getStatusJSON();
  });

  server.on("/fwrainbow", []() {
    //exit_func = true;
    settings.mode = FIREWORKS_RAINBOW;
    getArgs();
    getStatusJSON();
  });
  
  server.on("/colorflow", []() {
    //exit_func = true;
    settings.mode = COLORFLOW;
    getArgs();
    getStatusJSON();
  });

  server.on("/caleidoscope1", []() {
    //exit_func = true;
    settings.mode = CALEIDOSCOPE1;
    getArgs();
    getStatusJSON();
  });

  server.on("/caleidoscope2", []() {
    //exit_func = true;
    settings.mode = CALEIDOSCOPE2;
    getArgs();
    getStatusJSON();
  });

  server.on("/caleidoscope3", []() {
    //exit_func = true;
    settings.mode = CALEIDOSCOPE3;
    getArgs();
    getStatusJSON();
  });

  server.on("/caleidoscope4", []() {
    //exit_func = true;
    settings.mode = CALEIDOSCOPE4;
    getArgs();
    getStatusJSON();
  });

  #ifdef HTTP_OTA
    httpUpdater.setup(&server,"/update");
  #endif

  server.begin();

  clockAppearTimer = millis() + (settings.clock_timer * 1000);
  textAppearTimer = millis() + (settings.text_timer * 1000);
}

void loop() {
  EVERY_N_MILLISECONDS(int(float(1000 / settings.fps))) {
    gHue++;  // slowly cycle the "base color" through the rainbow
  }

  // Simple statemachine that handles the different modes
  switch (settings.mode) {
    default:
    case OFF:
    case BLANK:
      fill_solid(leds[0], NUM_LEDS, CRGB(0,0,0));
      break;
      
    case ALL: 
      fill_solid(leds[0], NUM_LEDS,  CRGB(settings.main_color.red, settings.main_color.green,
                         settings.main_color.blue));
      break;

    case MIXEDSHOW: 
      {     
        gPatterns[gCurrentPatternNumber]();
      
        // send the 'leds' array out to the actual LED strip
        int showlength_Millis = settings.show_length * 1000;

        // DBG_OUTPUT_PORT.println("showlengthmillis = " +
        // String(showlength_Millis));
        if (((millis()) - (lastMillis)) >= showlength_Millis) {
          nextPattern();
          DBG_OUTPUT_PORT.println( "void nextPattern was called at " + String(millis()) +
            " and the current show length set to " + String(showlength_Millis));
        }
      }
      break;
      
    case RAINBOW:
      rainbow();
      break;

    case CONFETTI:
      confetti();
      break;

    case SINELON:
      sinelon();
      break;

    case JUGGLE:
      juggle();
      break;

    case BPM:
      bpm();
      break;

    case RIPPLE:
      ripple();
      break;

    case COMET:
      comet();
      break;

    case THEATERCHASE:
      theaterChase();
      break;

    case WIPE:
      colorWipe();
      break;

    case TV:
      tv();
      break;
    
    case FIRE:
      fire2012();
      break;

    case FIRE_RAINBOW:
      fire_rainbow();
      break;

    case FIREWORKS:
      fireworks();
      break;
    
    case FIREWORKS_SINGLE:
      fw_single();
      break;
    
    case FIREWORKS_RAINBOW:
      fw_rainbow();
      break;

    case COLORFLOW:
      colorflow();
      break;

    case CALEIDOSCOPE1:
      caleidoscope(1);
      break;

    case CALEIDOSCOPE2:
      caleidoscope(2);
      break;

    case CALEIDOSCOPE3:
      caleidoscope(3);
      break;

    case CALEIDOSCOPE4:
      caleidoscope(4);
      break;
}

  // Add glitter if necessary
  if (settings.glitter_on == true && settings.mode != OFF) {
    addGlitter(settings.glitter_density);
  }

  //Update NTP-Time if clock is active
  if (settings.show_clock == true && settings.mode != OFF) {
    timeClient.update();
  }

  // Init clock if enabled and not currently running
  if (settings.mode != OFF && settings.show_clock == true && showClock == false && showText == false && TextLoaded == false && clockAppearTimer <= millis()) {
    initClock();
  }

  // Init text if enabled and not currently running
  if (settings.mode != OFF && settings.show_text == true && showClock == false && showText == false && TextLoaded == false && textAppearTimer <= millis()) {
    initText();
  }

  // Init loaded text if clock is not running (preview)
  if (TextLoaded == true && showClock == false && showText == false) {
    initText();
  }
  
  // Get the current time
  unsigned long continueTime = millis() + int(float(1000 / settings.fps));  

  // Do our main loop functions, until we hit our wait time
  do {
    //long int now = micros();

    // Handle clock or text if neccessary
    if (showClock == true || showText == true) {
      if (ScrollingMsg.UpdateText() == -1) {
        if (showClock == true) {
          clockAppearTimer = millis() + (settings.clock_timer * 1000);
          showClock = false;
        }
        if (showText == true) {
          textAppearTimer = millis() + (settings.text_timer * 1000);
          showText = false;
        }
      }
    }

    FastLED.show();         // Display whats rendered.    
    //long int later = micros();
    //DBG_OUTPUT_PORT.printf("Show time is %ld\n", later-now);
    server.handleClient();  // Handle requests to the web server
    webSocket.loop();       // Handle websocket traffic

    #ifdef ENABLE_OTA
      ArduinoOTA.handle();    // Handle OTA requests.
    #endif
    #ifdef REMOTE_DEBUG
      Debug.handle();         // Handle telnet server
    #endif         
    yield();                // Yield for ESP8266 stuff

    if (WiFi.status() != WL_CONNECTED) {
      // Blink the LED quickly to indicate WiFi connection lost.
      ticker.attach(0.1, tick);
     
      //EVERY_N_MILLISECONDS(1000) {
      //  int state = digitalRead(BUILTIN_LED);  // get the current state of GPIO1 pin      
      //  digitalWrite(BUILTIN_LED, !state);
      // }
    } else {      
      ticker.detach();
      // Light on-steady indicating WiFi is connected.
      //digitalWrite(BUILTIN_LED, false);
    }
    
  } while (millis() < continueTime);
}

void nextPattern() {
  // add one to the current pattern number, and wrap around at the end
  //  gCurrentPatternNumber = (gCurrentPatternNumber + random(0,
  //  ARRAY_SIZE(gPatterns))) % ARRAY_SIZE( gPatterns);
  gCurrentPatternNumber = random(0, ARRAY_SIZE(gPatterns));
  lastMillis = millis();
}
