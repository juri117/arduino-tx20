/*
  Created by Juri, June 15, 2021.
  Released under GPL-2.0 License
*/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>

const char* SSID = "<ssid>";
const char* PSK = "<wifi_pw>";
const char* MQTT_BROKER = "<host>";

/*
 * the input pin on the arduino
 */
const byte DATAPIN = 4;
/*
 * print detailed error messages
 */
const bool VERBOSE = false;
/*
 * due to a transistor the logic might be inverted
 * on first try activate VERBOSE and check, if all data is 1 set this to true
 */
const bool INVERSE_LOGIC = false;

const unsigned long MQTT_PUBLISH_INTERVAL_MS = 60000;
unsigned long last_sent_ms = 0;

const uint32_t NO_DATA_TIMEOUT_MS = 5000;
String raw_data_buff = "";
uint64_t time_ref_us;
uint64_t last_event_time_ms = 0;
uint16_t wind_speed;
uint16_t wind_dir;

WiFiClient espClient;
PubSubClient client(espClient);

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(DATAPIN, INPUT);
  Serial.begin(115200);
  Serial.println("starting, direction in ddeg, speed in dm/s");
  last_event_time_ms = millis();
  setup_wifi();
  client.setServer(MQTT_BROKER, 1883);
}

void setup_wifi() {
  WiFi.begin(SSID, PSK);
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
  }
  Serial.println(WiFi.localIP());
}

void onConnectionEstablished() { Serial.println("mqtt connected!"); }

void wait_rel_us(uint64_t rel_time_us) {
  delayMicroseconds(rel_time_us - (micros() - time_ref_us));
  time_ref_us += rel_time_us;
}

boolean parse_data() {
  int bitcount = 0;
  raw_data_buff = "";
  uint8_t buff_pointer = 0;
  uint64_t bit_buff = 0;

  for (bitcount = 41; bitcount > 0; bitcount--) {
    uint8_t pin = digitalRead(DATAPIN);
    if (INVERSE_LOGIC) {
      if (!pin) {
        raw_data_buff += "1";
        bit_buff += (uint64_t)1 << buff_pointer;
      } else {
        raw_data_buff += "0";
      }
    } else {
      if (pin) {
        raw_data_buff += "1";
        bit_buff += (uint64_t)1 << buff_pointer;
      } else {
        raw_data_buff += "0";
      }
    }
    buff_pointer++;
    wait_rel_us(1220);
  }

  uint8_t start = bit_buff & 0b11111;
  if (start != 0b11011) {
    if (VERBOSE) {
      char a[90];
      sprintf(a, "wrong start frame (%d != %d)", start, 0b11011);
      Serial.println(a);
      Serial.println(raw_data_buff);
    }
    return false;
  }

  uint8_t dir = (~(bit_buff >> 5)) & 0b1111;
  uint16_t speed = (~(bit_buff >> (5 + 4))) & 0b111111111111;
  uint8_t chk = (~(bit_buff >> (5 + 4 + 12))) & 0b1111;
  uint8_t dir2 = (bit_buff >> (5 + 4 + 12 + 4)) & 0b1111;
  uint16_t speed2 = (bit_buff >> (5 + 4 + 12 + 4 + 4)) & 0b111111111111;

  uint8_t chk_calc = (dir + (speed & 0b1111) + ((speed >> 4) & 0b1111) +
                      ((speed >> 8) & 0b1111)) &
                     0b1111;
  if (dir != dir2) {
    if (VERBOSE) {
      char a[90];
      sprintf(a, "cwind speed inconsistent (%d != %d)", speed, speed2);
      Serial.println(a);
      Serial.println(raw_data_buff);
    }
    return false;
  }
  if (speed != speed2) {
    if (VERBOSE) {
      char a[90];
      sprintf(a, "cwind speed inconsistent (%d != %d)", speed, speed2);
      Serial.println(a);
      Serial.println(raw_data_buff);
    }
    return false;
  }
  if (chk_calc != chk) {
    if (VERBOSE) {
      char a[90];
      sprintf(a, "checksum incorrect (%d != %d)", chk_calc, chk);
      Serial.println(a);
      Serial.println(raw_data_buff);
    }
    return false;
  }
  wind_speed = speed;
  wind_dir = dir * 225;
  return true;
}

void loop() {
  if (!client.connected()) {
    while (!client.connected()) {
      Serial.println("try to connect to broker");
      client.connect("weatherGod");
      digitalWrite(LED_BUILTIN, HIGH);
      delay(100);
      digitalWrite(LED_BUILTIN, LOW);
    }
  }
  if ((digitalRead(DATAPIN) && !INVERSE_LOGIC) or
      (!digitalRead(DATAPIN) && INVERSE_LOGIC)) {
    time_ref_us = micros();
    wait_rel_us(300);  // wait to prevent measuring on the edge of the signals
    char a[200];
    boolean validData = parse_data();
    if (!validData) {
      Serial.println("fail,could not parse data");
      delay(1000);
    } else {
      sprintf(a, "ok,%d,%d", wind_dir, wind_speed);
      Serial.println(a);
      if (millis() > last_sent_ms + MQTT_PUBLISH_INTERVAL_MS) {
        digitalWrite(LED_BUILTIN, HIGH);
        sprintf(a,
                "{\"measurements\":[{\"value\":%f,\"type_id\":6},{\"value\": "
                "%f,\"type_id\":7}]}",
                wind_dir / 10., wind_speed / 10.);
        client.publish("3/measurement", a);
        last_sent_ms = millis();
        digitalWrite(LED_BUILTIN, LOW);
      }
    }
    delay(10);  // to prevent getting triggered again
    last_event_time_ms = millis();
  }
  if (millis() - last_event_time_ms >= NO_DATA_TIMEOUT_MS) {
    Serial.println("fail,timout");
    last_event_time_ms = millis();
  }
  client.loop();
}
