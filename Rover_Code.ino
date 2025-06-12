#include <SPI.h>
#include <WiFi101.h>

// Coordinate system variables
int xCoordinate = 0;
int yCoordinate = 0;
int speedValue = 150;  // Default speed value

// Motor control pins
const int motorPWM1 = 3;   // PWM pin for motor 1
const int motorPWM2 = 4;  // PWM pin for motor 2
const int motorDir1 = 8;   // Direction pin for motor 1
const int motorDir2 = 9;   // Direction pin for motor 2

// Function return values
String ultrasoundValue = "no signal";
unsigned long speciesValue = 0;

// Ken's code - Infrared detection
volatile bool newPulse = false;
volatile unsigned long pulseinterval;
volatile unsigned long time1;
volatile unsigned long time2;
volatile int pulseCount;

// Radio detection variables
const byte radioInputPin = 6;
const float minFrequency = 80.0;
const float maxFrequency = 600.0;
const int samplesToAverage = 1;
volatile unsigned long radioLastTime = 0;
volatile unsigned long radioPeriodSum = 0;
volatile int radioSampleCount = 0;
volatile boolean radioDoneAveraging = false;
String radio_status = "no signal detected";

// Philip's code
int US_count=0;
bool ultrasound_current_name_valid=false;
String ultrasound_name_temp = "####";
String ultrasound_name_current = "####";
unsigned long long name_timestamp = 0;
bool ultrasound_connected = false;

// Infrared status tracking
String infrared_status = "no signal detected";
unsigned long lastValidFrequency = 0;
unsigned long lastInfraredUpdate = 0;

// WiFi credentials
char ssid[] = "HongZhe";
char pass[] = "999999999";
int status = WL_IDLE_STATUS;

// Create server instance on port 80
WiFiServer server(80);

// Cycle timing
unsigned long lastCycleDisplay = 0;
const unsigned long CYCLE_DISPLAY_INTERVAL = 1000; // Display every 1 second

bool is_valid_char(char c){
  String valid = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  if (valid.indexOf(c)<0){
    return false;
  }
  return true;
}

String get_ultrasound(){
  if(ultrasound_connected && ultrasound_name_current != "####") {
    return ultrasound_name_current;
  }
  return "no signal";
}

void read_ultrasound(){
  bool data_available = false;
  while(Serial1.available()){
    data_available = true;
    ultrasound_connected = true;
    char c=Serial1.read();
    if (c=='#'){
      US_count=0;
      ultrasound_current_name_valid=true;
    } else if (US_count==4 || !is_valid_char(c)){
      ultrasound_current_name_valid=false;
    } else if(ultrasound_current_name_valid){
      ultrasound_name_temp[US_count]=c;
    }
    US_count++;
    if (ultrasound_current_name_valid && US_count==4){
      ultrasound_name_current=ultrasound_name_temp;
      name_timestamp=millis();
    }
  }
  
  // If no data available for a while, mark as disconnected
  if(!data_available) {
    static unsigned long lastDataTime = 0;
    if(millis() - lastDataTime > 2000) { // 2 seconds timeout
      ultrasound_connected = false;
    }
  } else {
    static unsigned long lastDataTime = millis();
  }
}

String species_setup() {
  return "def";
}

// Joystick control curve function
float joystickCurve(float input) {
  float p = input/255;
  float ans = sin((3.1415/2)*p); // SINUSOIDAL MODE
  return ans;
}

void avt_loop(int J_x, int J_y) {
  float x = (joystickCurve(J_x)*255);
  float y = (joystickCurve(J_y)*255);

  float L_pwm = ((y+x)/360)*255;
  float R_pwm = ((y-x)/360)*255;

  // Calculate PWM values for each motor
  if (L_pwm > 0) {
      analogWrite(motorPWM1, L_pwm);
      digitalWrite(motorDir1, LOW);
  } else {
      analogWrite(motorPWM1, -L_pwm);
      digitalWrite(motorDir1, HIGH);
  }

  if (R_pwm > 0) {
      analogWrite(motorPWM2, R_pwm);
      digitalWrite(motorDir2, LOW);
  } else {
      analogWrite(motorPWM2, -R_pwm);
      digitalWrite(motorDir2, HIGH);
  }
}

void setup() {
  // Initialize motor pins
  pinMode(motorPWM1, OUTPUT);
  pinMode(motorPWM2, OUTPUT);
  pinMode(motorDir1, OUTPUT);
  pinMode(motorDir2, OUTPUT);
  
  // Start with motors stopped
  avt_loop(0, 0);
  
  Serial.begin(115200);
  Serial1.begin(600);
  delay(2000);
  
  Serial.println("WiFi Joystick Controller - Metro M0 Express + WINC1500");
  Serial.println("🤖 Motor control enabled!");
  
  // Check for WiFi module
  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println("WiFi shield not present");
    while (true);
  }
  
  // Connect to WiFi
  connectToWiFi();
  
  // Start the web server
  server.begin();
  Serial.print("HTTP server started at: ");
  Serial.println(WiFi.localIP());
  Serial.println("Ready to receive joystick commands!");

  // Ken's code - Infrared on pin 2
  pinMode(2,INPUT);
  attachInterrupt(digitalPinToInterrupt(2),PulseIn,RISING);
  
  // Radio detection on pin 7
  pinMode(radioInputPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(radioInputPin), onRadioPulse, RISING);
  
  Serial.println("\n=== Starting Status Display ===");
}

// Ken's code - Infrared ISR
void PulseIn(){
  time1 = micros();
  pulseinterval = time1 - time2;
  time2 = time1;
  newPulse = true;
}

// Radio ISR: Captures AM pulses
void onRadioPulse() {
  unsigned long now = micros();
  unsigned long period = now - radioLastTime;
  if (period > 1500 && period < 12500) {
    radioPeriodSum += period;
    radioSampleCount++;
    if (radioSampleCount >= samplesToAverage) {
      radioDoneAveraging = true;
    }
  }
  radioLastTime = now;
}

// Radio detection function
String get_radio() {
  // Reset accumulators
  noInterrupts();
  radioPeriodSum = 0;
  radioSampleCount = 0;
  radioDoneAveraging = false;
  radioLastTime = micros();
  unsigned long startTime = millis();
  interrupts();
  
  // Wait for 100 samples with timeout
  while (!radioDoneAveraging) {
    if (millis() - startTime > 1000) {
      radio_status = "no signal (timeout)";
      return radio_status;
    }
  }
  
  noInterrupts();
  unsigned long avgPeriod = radioPeriodSum / samplesToAverage;
  radioDoneAveraging = false;
  interrupts();
  
  if (avgPeriod > 0) {
    float frequency = 1000000.0 / avgPeriod;
    if (frequency >= minFrequency && frequency <= maxFrequency) {
      radio_status = String(frequency, 2) + " Hz";
      return radio_status;
    } else {
      radio_status = "out of range: " + String(frequency, 2) + " Hz";
      return radio_status;
    }
  }
  
  radio_status = "no signal";
  return radio_status;
}
// Updated infrared detection function
unsigned long get_infrared() {
  if(pulseCount == 0){
    pulseCount += 1;
  }
  
  if(newPulse){
    noInterrupts();
    unsigned long timeInterval = pulseinterval;
    newPulse = false;
    interrupts();
    
    unsigned long frequency = 1000000/timeInterval;
    lastInfraredUpdate = millis();
    
    // Check for 293 Hz (±10 Hz range: 283-303)
    if(frequency >= 283 && frequency <= 303) {
      infrared_status = "Signal within ±10 range (293Hz target): " + String(frequency) + "Hz";
      lastValidFrequency = frequency;
      return frequency;
    }
    // Check for 457 Hz (±10 Hz range: 447-467)
    else if(frequency >= 447 && frequency <= 467) {
      infrared_status = "Signal within ±10 range (457Hz target): " + String(frequency) + "Hz";
      lastValidFrequency = frequency;
      return frequency;
    }
    else {
      infrared_status = "out of range: " + String(frequency) + "Hz";
      return lastValidFrequency;
    }
  }
  
  // If no new pulse for a while, mark as no signal
  if(millis() - lastInfraredUpdate > 3000) { // 3 seconds timeout
    infrared_status = "no signal detected";
  }
  
  return lastValidFrequency;
}

void displayStatus() {
  Serial.println("\n--- Cycle Status ---");
  Serial.println("Joystick: X=" + String(xCoordinate) + ", Y=" + String(yCoordinate));
  Serial.println("Ultrasound: " + get_ultrasound());
  Serial.println("Infrared: " + infrared_status);
  Serial.println("Radio: " + radio_status);
  Serial.println("-------------------");
}

void loop() {
  // Read sensors
  read_ultrasound();
  ultrasoundValue = get_ultrasound();
  speciesValue = get_infrared();
  
  // Update radio status (non-blocking check)
  static unsigned long lastRadioCheck = 0;
  if (millis() - lastRadioCheck >= 2000) { // Check radio every 2 seconds
    get_radio(); // This updates radio_status
    lastRadioCheck = millis();
  }
  
  // Display status every cycle (with timing control)
  if (millis() - lastCycleDisplay >= CYCLE_DISPLAY_INTERVAL) {
    displayStatus();
    lastCycleDisplay = millis();
  }
  
  // Listen for incoming clients
  WiFiClient client = server.available();
  
  if (client) {
    String request = "";
    
    // Add timeout to prevent blocking
    unsigned long clientTimeout = millis();
    const unsigned long CLIENT_TIMEOUT_MS = 3000; // 3 second timeout
    
    // Read the HTTP request with timeout
    while (client.connected() && (millis() - clientTimeout < CLIENT_TIMEOUT_MS)) {
      if (client.available()) {
        String line = client.readStringUntil('\r');
        if (line.startsWith("GET")) {
          request = line;
          break;
        }
      }
      delay(1); // Small delay to prevent tight loop
    }
    
    // Check if we got a valid request
    if (request.length() > 0) {
      // Clear any remaining data quickly
      unsigned long clearStart = millis();
      while (client.available() && (millis() - clearStart < 100)) {
        client.read();
      }
      
      // Process the command
      processCommand(request, client);
    }
    
    // Close the connection
    client.stop();
  }
  
  // Check WiFi connection periodically
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 10000) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi disconnected! Resetting coordinates...");
      xCoordinate = 0;
      yCoordinate = 0;
      connectToWiFi();
    }
    lastCheck = millis();
  }
}

void connectToWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  
  status = WiFi.begin(ssid, pass);
  
  // Wait for connection
  int attempts = 0;
  while (status != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    status = WiFi.status();
    attempts++;
  }
  
  if (status == WL_CONNECTED) {
    Serial.println();
    Serial.println("✓ WiFi connected!");
    
    // Get and print IP address
    IPAddress ip = WiFi.localIP();
    Serial.print("IP address: ");
    Serial.println(ip);
    
    Serial.print("Signal strength: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("\n✗ Failed to connect to WiFi!");
  }
}

void processCommand(String request, WiFiClient client) {
  // Parse the request
  if (request.indexOf("GET / ") >= 0) {
    // Serve main page with joystick
    sendJoystickPage(client);
  } else if (request.indexOf("GET /joystick") >= 0) {
    // Handle joystick coordinate updates
    handleJoystickUpdate(request, client);
  } else if (request.indexOf("GET /getcoordinates") >= 0) {
    // Send current coordinates as JSON
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    client.print("{\"x\":");
    client.print(xCoordinate);
    client.print(",\"y\":");
    client.print(yCoordinate);
    client.println("}");
  } else if (request.indexOf("GET /getdata") >= 0) {
    // Send ultrasound and species data as JSON
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    client.print("{\"ultrasound\":\"");
    client.print(ultrasoundValue);
    client.print("\",\"species\":");
    client.print(speciesValue);
    client.println("}");
  } else {
    // 404 Not Found
    client.println("HTTP/1.1 404 Not Found");
    client.println("Content-Type: text/plain");
    client.println("Connection: close");
    client.println();
    client.println("Route not found");
  }
}

void handleJoystickUpdate(String request, WiFiClient client) {
  // Extract X and Y values from URL: /joystick?x=123&y=456
  int xStart = request.indexOf("x=");
  int yStart = request.indexOf("y=");
  
  if (xStart > 0 && yStart > 0) {
    xStart += 2; // Skip "x="
    yStart += 2; // Skip "y="
    
    // Find end of X value
    int xEnd = request.indexOf("&", xStart);
    if (xEnd < 0) xEnd = request.indexOf(" ", xStart);
    
    // Find end of Y value  
    int yEnd = request.indexOf(" ", yStart);
    if (yEnd < 0) yEnd = request.indexOf("&", yStart);
    if (yEnd < 0) yEnd = request.length();
    
    // Extract coordinate strings
    String xStr = request.substring(xStart, xEnd);
    String yStr = request.substring(yStart, yEnd);
    
    // Convert to integers
    int newX = xStr.toInt();
    int newY = yStr.toInt();
    
    // Validate range (-255 to 255)
    if (newX >= -255 && newX <= 255 && newY >= -255 && newY <= 255) {
      xCoordinate = newX;
      yCoordinate = newY;
      
      // Print joystick coordinates when they change
      Serial.println("---JOYSTICK UPDATE: X=" + String(newX) + ", Y=" + String(newY) + "---");
      
      avt_loop(newX, newY);
      
      // Send success response
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: text/plain");
      client.println("Connection: close");
      client.println();
      client.println("OK");
    } else {
      client.println("HTTP/1.1 400 Bad Request");
      client.println("Content-Type: text/plain");
      client.println("Connection: close");
      client.println();
      client.println("Invalid coordinates");
    }
  } else {
    client.println("HTTP/1.1 400 Bad Request");
    client.println("Content-Type: text/plain");
    client.println("Connection: close");
    client.println();
    client.println("Missing coordinates");
  }
}

void sendJoystickPage(WiFiClient client) {
  // Get IP address as string
  IPAddress ip = WiFi.localIP();
  
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  
  // Send HTML page with WiFi joystick
  client.println(F("<!DOCTYPE HTML>"));
  client.println(F("<html>"));
  client.println(F("<head>"));
  client.println(F("<title>WiFi Joystick Controller</title>"));
  client.println(F("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"));
  client.println(F("<style>"));
  client.println(F("body {"));
  client.println(F("  font-family: Arial, sans-serif;"));
  client.println(F("  text-align: center;"));
  client.println(F("  margin: 0;"));
  client.println(F("  padding: 20px;"));
  client.println(F("  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);"));
  client.println(F("  min-height: 100vh;"));
  client.println(F("  display: flex;"));
  client.println(F("  flex-direction: column;"));
  client.println(F("  justify-content: center;"));
  client.println(F("  align-items: center;"));
  client.println(F("}"));
  client.println(F(".container {"));
  client.println(F("  background-color: white;"));
  client.println(F("  padding: 40px;"));
  client.println(F("  border-radius: 20px;"));
  client.println(F("  box-shadow: 0 8px 25px rgba(0,0,0,0.15);"));
  client.println(F("  max-width: 500px;"));
  client.println(F("}"));
  client.println(F("h1 { color: #333; margin-bottom: 30px; }"));
  client.println(F(".joystick-container {"));
  client.println(F("  position: relative;"));
  client.println(F("  width: 300px;"));
  client.println(F("  height: 300px;"));
  client.println(F("  margin: 20px auto;"));
  client.println(F("  background: #f0f0f0;"));
  client.println(F("  border-radius: 50%;"));
  client.println(F("  border: 3px solid #ccc;"));
  client.println(F("  box-shadow: inset 0 4px 10px rgba(0,0,0,0.1);"));
  client.println(F("}"));
  client.println(F(".joystick-knob {"));
  client.println(F("  position: absolute;"));
  client.println(F("  width: 60px;"));
  client.println(F("  height: 60px;"));
  client.println(F("  background: linear-gradient(135deg, #28a745 0%, #20c997 100%);"));
  client.println(F("  border-radius: 50%;"));
  client.println(F("  cursor: pointer;"));
  client.println(F("  box-shadow: 0 4px 15px rgba(0,0,0,0.3);"));
  client.println(F("  top: 50%;"));
  client.println(F("  left: 50%;"));
  client.println(F("  transform: translate(-50%, -50%);"));
  client.println(F("  transition: box-shadow 0.2s ease;"));
  client.println(F("  border: 3px solid white;"));
  client.println(F("  z-index: 10;"));
  client.println(F("}"));
  client.println(F(".data-display {"));
  client.println(F("  margin: 20px 0;"));
  client.println(F("  padding: 20px;"));
  client.println(F("  background: #f8f9fa;"));
  client.println(F("  border-radius: 15px;"));
  client.println(F("  border: 2px solid #e9ecef;"));
  client.println(F("}"));
  client.println(F(".data-item {"));
  client.println(F("  display: flex;"));
  client.println(F("  justify-content: space-between;"));
  client.println(F("  margin: 10px 0;"));
  client.println(F("  padding: 15px;"));
  client.println(F("  background: white;"));
  client.println(F("  border-radius: 10px;"));
  client.println(F("  box-shadow: 0 2px 5px rgba(0,0,0,0.1);"));
  client.println(F("}"));
  client.println(F(".data-label {"));
  client.println(F("  font-weight: bold;"));
  client.println(F("  color: #333;"));
  client.println(F("}"));
  client.println(F(".data-value {"));
  client.println(F("  color: #007bff;"));
  client.println(F("  font-family: monospace;"));
  client.println(F("  font-size: 16px;"));
  client.println(F("}"));
  client.println(F(".coordinates-display {"));
  client.println(F("  font-size: 24px;"));
  client.println(F("  font-weight: bold;"));
  client.println(F("  margin: 30px 0;"));
  client.println(F("  padding: 20px;"));
  client.println(F("  background: #f8f9fa;"));
  client.println(F("  border-radius: 15px;"));
  client.println(F("}"));
  client.println(F(".reset-btn {"));
  client.println(F("  background: #dc3545;"));
  client.println(F("  color: white;"));
  client.println(F("  border: none;"));
  client.println(F("  padding: 12px 25px;"));
  client.println(F("  border-radius: 10px;"));
  client.println(F("  font-size: 16px;"));
  client.println(F("  font-weight: bold;"));
  client.println(F("  cursor: pointer;"));
  client.println(F("  margin-top: 20px;"));
  client.println(F("  transition: all 0.3s ease;"));
  client.println(F("}"));
  client.println(F(".reset-btn:hover {"));
  client.println(F("  background: #c82333;"));
  client.println(F("  transform: translateY(-2px);"));
  client.println(F("}"));
  client.println(F(".status {"));
  client.println(F("  margin-top: 20px;"));
  client.println(F("  padding: 15px;"));
  client.println(F("  background: #e7f3ff;"));
  client.println(F("  border-radius: 10px;"));
  client.println(F("  font-size: 14px;"));
  client.println(F("}"));
  client.println(F("</style>"));
  client.println(F("</head>"));
  client.println(F("<body>"));
  client.println(F("<div class=\"container\">"));
  client.println(F("<h1>WiFi Joystick Controller</h1>"));
  
  client.println(F("<div class=\"joystick-container\" id=\"joystickContainer\">"));
  client.println(F("<div class=\"joystick-knob\" id=\"joystickKnob\"></div>"));
  client.println(F("</div>"));
  
  client.println(F("<div class=\"coordinates-display\">"));
  client.println(F("X: <span id=\"xValue\">0</span>, Y: <span id=\"yValue\">0</span>"));
  client.println(F("</div>"));
  
  client.println(F("<div class=\"data-display\">"));
  client.println(F("<h3 style=\"margin-top: 0; color: #333;\">Sensor Data</h3>"));
  client.println(F("<div class=\"data-item\">"));
  client.println(F("<span class=\"data-label\">Ultrasound:</span>"));
  client.println(F("<span class=\"data-value\" id=\"ultrasoundValue\">Loading...</span>"));
  client.println(F("</div>"));
  client.println(F("<div class=\"data-item\">"));
  client.println(F("<span class=\"data-label\">Species:</span>"));
  client.println(F("<span class=\"data-value\" id=\"speciesValue\">Loading...</span>"));
  client.println(F("</div>"));
  client.println(F("</div>"));
  
  client.println(F("<button class=\"reset-btn\" onclick=\"resetJoystick()\">Reset to Center</button>"));
  
  client.println(F("<div class=\"status\">"));
  client.println(F("Connected to Arduino at: "));
  client.print(ip[0]); client.print(".");
  client.print(ip[1]); client.print(".");
  client.print(ip[2]); client.print(".");
  client.print(ip[3]);
  client.println(F("<br>Check Serial Monitor for coordinate output!"));
  client.println(F("</div>"));
  
  client.println(F("<script>"));
  client.println(F("const joystickContainer = document.getElementById('joystickContainer');"));
  client.println(F("const joystickKnob = document.getElementById('joystickKnob');"));
  client.println(F("const xValueDisplay = document.getElementById('xValue');"));
  client.println(F("const yValueDisplay = document.getElementById('yValue');"));
  client.println(F("let isDragging = false;"));
  client.println(F("let containerRect, centerX, centerY, maxRadius;"));
  client.println(F("let lastSentTime = 0;"));
  
  client.println(F("function initJoystick() {"));
  client.println(F("  containerRect = joystickContainer.getBoundingClientRect();"));
  client.println(F("  centerX = containerRect.width / 2;"));
  client.println(F("  centerY = containerRect.height / 2;"));
  client.println(F("  maxRadius = (containerRect.width / 2) - 30;"));
  client.println(F("}"));
  
  client.println(F("function sendCoordinates(x, y) {"));
  client.println(F("  const now = Date.now();"));
  client.println(F("  if (now - lastSentTime > 100) {"));
  client.println(F("    fetch('/joystick?x=' + x + '&y=' + y)"));
  client.println(F("      .catch(error => console.error('Error:', error));"));
  client.println(F("    lastSentTime = now;"));
  client.println(F("  }"));
  client.println(F("}"));
  
  client.println(F("function updateSensorData() {"));
  client.println(F("  fetch('/getdata')"));
  client.println(F("    .then(response => response.json())"));
  client.println(F("    .then(data => {"));
  client.println(F("      document.getElementById('ultrasoundValue').textContent = data.ultrasound;"));
  client.println(F("      document.getElementById('speciesValue').textContent = data.species;"));
  client.println(F("    })"));
  client.println(F("    .catch(error => console.error('Error fetching sensor data:', error));"));
  client.println(F("}"));
  
  client.println(F("function updateJoystick(clientX, clientY) {"));
  client.println(F("  if (!isDragging) return;"));
  client.println(F("  const relativeX = clientX - containerRect.left - centerX;"));
  client.println(F("  const relativeY = clientY - containerRect.top - centerY;"));
  client.println(F("  const distance = Math.sqrt(relativeX * relativeX + relativeY * relativeY);"));
  client.println(F("  let finalX = relativeX, finalY = relativeY;"));
  client.println(F("  if (distance > maxRadius) {"));
  client.println(F("    const angle = Math.atan2(relativeY, relativeX);"));
  client.println(F("    finalX = Math.cos(angle) * maxRadius;"));
  client.println(F("    finalY = Math.sin(angle) * maxRadius;"));
  client.println(F("  }"));
  client.println(F("  joystickKnob.style.left = (centerX + finalX) + 'px';"));
  client.println(F("  joystickKnob.style.top = (centerY + finalY) + 'px';"));
  client.println(F("  const xCoord = Math.round((finalX / maxRadius) * 254);"));
  client.println(F("  const yCoord = Math.round((-finalY / maxRadius) * 254);"));
  client.println(F("  xValueDisplay.textContent = xCoord;"));
  client.println(F("  yValueDisplay.textContent = yCoord;"));
  client.println(F("  sendCoordinates(xCoord, yCoord);"));
  client.println(F("}"));
  
  client.println(F("function resetJoystick() {"));
  client.println(F("  joystickKnob.style.left = '50%';"));
  client.println(F("  joystickKnob.style.top = '50%';"));
  client.println(F("  joystickKnob.style.transform = 'translate(-50%, -50%)';"));
  client.println(F("  xValueDisplay.textContent = '0';"));
  client.println(F("  yValueDisplay.textContent = '0';"));
  client.println(F("  isDragging = false;"));
  client.println(F("  sendCoordinates(0, 0);"));
  client.println(F("}"));
  
  client.println(F("joystickKnob.addEventListener('mousedown', function(e) {"));
  client.println(F("  isDragging = true; initJoystick(); e.preventDefault();"));
  client.println(F("});"));
  client.println(F("document.addEventListener('mousemove', function(e) {"));
  client.println(F("  updateJoystick(e.clientX, e.clientY);"));
  client.println(F("});"));
  client.println(F("document.addEventListener('mouseup', function() {"));
  client.println(F("  isDragging = false;"));
  client.println(F("});"));
  
  client.println(F("joystickKnob.addEventListener('touchstart', function(e) {"));
  client.println(F("  isDragging = true; initJoystick(); e.preventDefault();"));
  client.println(F("});"));
  client.println(F("document.addEventListener('touchmove', function(e) {"));
  client.println(F("  if (e.touches.length > 0) {"));
  client.println(F("    updateJoystick(e.touches[0].clientX, e.touches[0].clientY);"));
  client.println(F("  }"));
  client.println(F("  e.preventDefault();"));
  client.println(F("});"));
  client.println(F("document.addEventListener('touchend', function() {"));
  client.println(F("  isDragging = false;"));
  client.println(F("});"));
  
  client.println(F("window.addEventListener('load', initJoystick);"));
  client.println(F("updateSensorData();"));
  client.println(F("setInterval(updateSensorData, 1000);"));
  client.println(F("</script>"));
  
  client.println(F("</div>"));
  client.println(F("</body>"));
  client.println(F("</html>"));
}