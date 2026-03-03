/**
 * ESP8266 NodeMCU & Nextion NX8048T070 Display 7 inch 
 * Optimized for modern Pi-hole versions & ArduinoJson v7
 */

#include <Arduino.h>
#include <NTPClient.h>
#include <ArduinoJson.h>     // MUSS zwingend v6 oder v7 sein!
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>

#define ESPHostname "pihole-display" // Wi-Fi & OTA Hostname

WiFiClient client; // create wifi client object

const char * host = "192.168.XXX.XXX";    // Pi-Hole IP
const char * apiToken = "DEIN_PIHOLE_API_TOKEN"; // WICHTIG: API Token aus dem Pi-hole Webinterface (Settings -> API/Web interface -> Show API token)
const char * ssid = "YOUR-WIFI-SSID";     // Wi-Fi SSID Name
const char * password = "YOUR-WIFI-PASS"; // Wi-Fi Password
const char * otapassword = "YOUR-OTA-PASS"; // OTA Password

// UTC Offset in Sekunden anpassen
// Winterzeit (MEZ) = 3600 | Sommerzeit (MESZ) = 7200
const long utcOffsetInSeconds = 3600;

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, host, utcOffsetInSeconds);

// Timer-Variablen (ersetzen das blockierende delay)
unsigned long previousMillis = 0;
const long interval = 10000; // 10 Sekunden Refresh-Rate

// Funktionsdeklarationen
void sendNextionCommand(const String& cmd);
void fetchPiholeData();

void setup() {
  Serial.begin(115200);
  delay(1000);

  // WiFi Start
  WiFi.hostname(ESPHostname);
  WiFi.begin(ssid, password);
  
  int connAttempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    connAttempts++;
    if (connAttempts > 40) {
      ESP.restart(); // Neustart, falls WLAN nach 20 Sekunden nicht verbindet
    }
  }

  // OTA Setup
  ArduinoOTA.setPort(8266);
  ArduinoOTA.setHostname(ESPHostname);
  ArduinoOTA.setPassword(otapassword);
  ArduinoOTA.begin();
  
  // NTP Setup
  timeClient.begin();
}

void loop() {
  // OTA Handle muss zwingend extrem oft aufgerufen werden
  ArduinoOTA.handle();

  // Blockierungsfreier Timer für die Pi-hole Abfrage
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    if (WiFi.status() == WL_CONNECTED) {
      timeClient.update();
      fetchPiholeData();
    } else {
      sendNextionCommand("start_page.status.txt=\"No WiFi\"");
    }
  }
}

void fetchPiholeData() {
  HTTPClient http;
  
  // URL für Pi-hole v5 (Standard)
  String url = "http://" + String(host) + "/api.php?summary&auth=" + String(apiToken);
  
  // HINWEIS FÜR PI-HOLE v6: 
  // Falls du bereits Pi-hole v6 nutzt, gibt es die api.php nicht mehr. 
  // Entkommentiere dann die folgende Zeile (und passe ggf. Authentifizierungs-Header an, falls dein Webinterface passwortgeschützt ist):
  // String url = "http://" + String(host) + "/api/stats/summary";

  http.begin(client, url);
  int httpCode = http.GET();

  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      // Nutzt das moderne ArduinoJson v7
      JsonDocument doc; 
      
      // Stream direkt auslesen (verhindert RAM-Überlauf bei großen JSON-Strings)
      DeserializationError error = deserializeJson(doc, http.getStream());

      if (!error) {
        // Werte sicher auslesen. Wenn ein Key fehlt, wird der Standardwert (z.B. "0") gesetzt.
        String domains_being_blocked = doc["domains_being_blocked"] | "0";
        String dns_queries_today = doc["dns_queries_today"] | "0";
        String ads_blocked_today = doc["ads_blocked_today"] | "0";
        String ads_percentage_today = doc["ads_percentage_today"] | "0.0";
        String clients_ever_seen = doc["clients_ever_seen"] | "0";
        String unique_clients = doc["unique_clients"] | "0";
        String status_pihole = doc["status"] | "unknown";

        String update_days = doc["gravity_last_updated"]["relative"]["days"] | "0";
        String update_hours = doc["gravity_last_updated"]["relative"]["hours"] | "0";
        String update_minutes = doc["gravity_last_updated"]["relative"]["minutes"] | "0";

        // Befehle generieren und ans Display senden
        sendNextionCommand("start_page.ads.txt=\"" + dns_queries_today + "\"");
        sendNextionCommand("start_page.clients.txt=\"(Clients: unique: " + unique_clients + " / ever seen: " + clients_ever_seen + ")\"");
        sendNextionCommand("start_page.domains.txt=\"" + domains_being_blocked + "\"");
        sendNextionCommand("start_page.today.txt=\"" + ads_percentage_today + "%\"");
        sendNextionCommand("start_page.blocked.txt=\"" + ads_blocked_today + "\"");
        sendNextionCommand("start_page.update.txt=\"(Last update: " + update_days + " days / " + update_hours + " hours / " + update_minutes + " minutes)\"");
        
        if (status_pihole == "enabled") {
          sendNextionCommand("start_page.status.pic=2");
        } else {
          sendNextionCommand("start_page.status.pic=1");
        }
        
        // Zeitstempel für den letzten erfolgreichen Update-Status setzen
        sendNextionCommand("start_page.status.txt=\"" + timeClient.getFormattedTime() + "\"");

      } else {
        // Fehler beim Parsen des JSON (z.B. falscher Token -> falsches Format)
        sendNextionCommand("start_page.status.txt=\"JSON Error\"");
      }
    } else {
      // HTTP-Fehlercode, z.B. 401 Unauthorized oder 404 Not Found
      sendNextionCommand("start_page.status.txt=\"HTTP " + String(httpCode) + "\"");
    }
  } else {
    // Keine Verbindung zum Pi-hole möglich
    sendNextionCommand("start_page.status.txt=\"Host unreach\"");
  }
  
  http.end();
}

// Hilfsfunktion: Befehl ans Display senden und mit 3x 0xFF terminieren
void sendNextionCommand(const String& cmd) {
  Serial.print(cmd);
  Serial.write(0xff);
  Serial.write(0xff);
  Serial.write(0xff);
}

