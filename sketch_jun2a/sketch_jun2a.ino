/*
  CABLAGE (I2C) :
    SSD1306 VCC -> 3V3
    SSD1306 GND -> GND
    SSD1306 SDA -> GPIO 21 (SDA par defaut sur la plupart des ESP32 dev boards)
    SSD1306 SCL -> GPIO 22 (SCL par defaut)

  LIBRAIRIES A INSTALLER (Arduino IDE > Outils > Gerer les bibliotheques) :
    - "Adafruit SSD1306"
    - "Adafruit GFX Library"
    (WiFi.h est deja inclus avec le coeur ESP32, rien d'autre a installer)

  CARTE :
    Outils > Type de carte > ESP32 Dev Module (ou celle qui correspond a ta carte)

  A CONFIGURER CI-DESSOUS :
    WIFI_SSID / WIFI_PASSWORD -> le WiFi auquel l'ESP32 doit se connecter
    (le PC doit etre sur le meme reseau).

  FONCTIONNEMENT :
    1. L'ESP32 se connecte au WiFi et affiche son adresse IP dans le
       Moniteur Serie (Outils > Moniteur Serie, 115200 bauds).
    2. L'ESP32 ouvre un serveur TCP sur le port TCP_PORT et attend une
       connexion.
    3. Le script Python se connecte a <IP_ESP32>:TCP_PORT et envoie un
       caractere par touche pressee :
         'U' -> haut, 'D' -> bas, 'L' -> gauche, 'R' -> droite,
         'X' -> reset / rejouer apres game over
*/

#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

const char* WIFI_SSID = "decode-etudiants";
const char* WIFI_PASSWORD = "learnByDoing25!";
const uint16_t TCP_PORT = 3333;

WiFiServer tcpServer(TCP_PORT);
WiFiClient tcpClient;

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

#define SDA_PIN 17
#define SCL_PIN 18

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const uint8_t TITLE_HEIGHT = 16;
const uint8_t PLAY_Y_OFFSET = TITLE_HEIGHT;

const uint8_t CELL_SIZE = 4;
const uint8_t GRID_COLS = SCREEN_WIDTH / CELL_SIZE;
const uint8_t GRID_ROWS = (SCREEN_HEIGHT - TITLE_HEIGHT) / CELL_SIZE;
const uint16_t MAX_SNAKE_LENGTH = GRID_COLS * GRID_ROWS;
const unsigned long MOVE_INTERVAL_MS = 150;

enum Direction { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT };

struct Point {
  int8_t x;
  int8_t y;
};

Point snake[MAX_SNAKE_LENGTH];
uint16_t snakeLength;
Direction currentDirection;
Direction pendingDirection;
Point food;
bool gameOver = false;
unsigned long lastMoveTime = 0;
uint16_t score = 0;


void placeFood() {
  bool onSnake;
  do {
    onSnake = false;
    food.x = random(0, GRID_COLS);
    food.y = random(0, GRID_ROWS);
    for (uint16_t i = 0; i < snakeLength; i++) {
      if (snake[i].x == food.x && snake[i].y == food.y) {
        onSnake = true;
        break;
      }
    }
  } while (onSnake);
}

void resetGame() {
  snakeLength = 3;
  snake[0] = { (int8_t)(GRID_COLS / 2), (int8_t)(GRID_ROWS / 2) };
  snake[1] = { (int8_t)(GRID_COLS / 2 - 1), (int8_t)(GRID_ROWS / 2) };
  snake[2] = { (int8_t)(GRID_COLS / 2 - 2), (int8_t)(GRID_ROWS / 2) };
  currentDirection = DIR_RIGHT;
  pendingDirection = DIR_RIGHT;
  score = 0;
  gameOver = false;
  placeFood();
  lastMoveTime = millis();
}

bool isOppositeDirection(Direction a, Direction b) {
  return (a == DIR_UP && b == DIR_DOWN) ||
         (a == DIR_DOWN && b == DIR_UP) ||
         (a == DIR_LEFT && b == DIR_RIGHT) ||
         (a == DIR_RIGHT && b == DIR_LEFT);
}

void handleDirectionInput(char c) {
  Direction requested;
  switch (c) {
    case 'U': requested = DIR_UP; break;
    case 'D': requested = DIR_DOWN; break;
    case 'L': requested = DIR_LEFT; break;
    case 'R': requested = DIR_RIGHT; break;
    case 'X':
      resetGame();
      return;
    default:
      return;
  }
  // on empeche de faire demi-tour direct sur le serpent
  if (!isOppositeDirection(requested, currentDirection)) {
    pendingDirection = requested;
  }
}

void updateGame() {
  if (gameOver) return;

  currentDirection = pendingDirection;

  Point newHead = snake[0];
  switch (currentDirection) {
    case DIR_UP:    newHead.y--; break;
    case DIR_DOWN:  newHead.y++; break;
    case DIR_LEFT:  newHead.x--; break;
    case DIR_RIGHT: newHead.x++; break;
  }

  // collision avec les murs
  if (newHead.x < 0 || newHead.x >= GRID_COLS || newHead.y < 0 || newHead.y >= GRID_ROWS) {
    gameOver = true;
    Serial.println("Game Over (mur) - score final: " + String(score));
    return;
  }

  // collision avec soi-meme
  for (uint16_t i = 0; i < snakeLength; i++) {
    if (snake[i].x == newHead.x && snake[i].y == newHead.y) {
      gameOver = true;
      Serial.println("Game Over (collision) - score final: " + String(score));
      return;
    }
  }

  bool ateFood = (newHead.x == food.x && newHead.y == food.y);

  // decale le corps (de la queue vers la tete), sauf si on mange (le serpent s'allonge)
  if (ateFood && snakeLength < MAX_SNAKE_LENGTH) {
    for (uint16_t i = snakeLength; i > 0; i--) {
      snake[i] = snake[i - 1];
    }
    snakeLength++;
    score++;
    placeFood();
  } else {
    for (uint16_t i = snakeLength - 1; i > 0; i--) {
      snake[i] = snake[i - 1];
    }
  }
  snake[0] = newHead;
}

// affiche le titre dans la bande du haut (jaune sur les ecrans bicolores)
void drawTitle() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(40, 4); 
  display.print("DJ SNAKE");
}

void drawGame() {
  display.clearDisplay();
  drawTitle();

  if (gameOver) {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(20, 20);
    display.println("GAME OVER");
    display.setCursor(15, 35);
    display.println("Score: " + String(score));
    display.setCursor(5, 50);
    display.println("Touche R = rejouer");
    display.display();
    return;
  }

  // nourriture (decalee sous la bande du titre)
  display.fillRect(food.x * CELL_SIZE, PLAY_Y_OFFSET + food.y * CELL_SIZE, CELL_SIZE, CELL_SIZE, SSD1306_WHITE);

  // serpent (decale sous la bande du titre)
  for (uint16_t i = 0; i < snakeLength; i++) {
    display.fillRect(snake[i].x * CELL_SIZE, PLAY_Y_OFFSET + snake[i].y * CELL_SIZE, CELL_SIZE - 1, CELL_SIZE - 1, SSD1306_WHITE);
  }

  display.display();
}

// ---------------------------------------------------------------------------
// traduit le code de statut WiFi en message lisible, pour diagnostiquer
const char* wifiStatusToString(wl_status_t status) {
  switch (status) {
    case WL_NO_SSID_AVAIL:  return "SSID introuvable (reseau invisible ou hors de portee / 5GHz uniquement ?)";
    case WL_CONNECT_FAILED: return "Echec de connexion (mot de passe probablement incorrect)";
    case WL_CONNECTION_LOST: return "Connexion perdue";
    case WL_DISCONNECTED:   return "Deconnecte";
    case WL_IDLE_STATUS:    return "En attente...";
    case WL_CONNECTED:      return "Connecte";
    default:                return "Code de statut inconnu";
  }
}

void connectWiFi() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Connexion WiFi...");
  display.display();

  WiFi.disconnect(true);
  delay(200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connexion au WiFi (SSID: ");
  Serial.print(WIFI_SSID);
  Serial.println(")");

  unsigned long startAttempt = millis();
  const unsigned long WIFI_TIMEOUT_MS = 15000;

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    wl_status_t status = WiFi.status();
    Serial.print("Statut WiFi: ");
    Serial.print(status);
    Serial.print(" (");
    Serial.print(wifiStatusToString(status));
    Serial.println(")");

    if (millis() - startAttempt > WIFI_TIMEOUT_MS) {
      Serial.println("Timeout WiFi, nouvelle tentative...");
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("WiFi echec,");
      display.println("nouvelle");
      display.println("tentative...");
      display.display();

      WiFi.disconnect(true);
      delay(500);
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      startAttempt = millis();
    }
  }

  Serial.print("Connecte ! IP de l'ESP32 : ");
  Serial.println(WiFi.localIP());

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("WiFi OK");
  display.println(WiFi.localIP());
  display.print("Port: "); display.println(TCP_PORT);
  display.display();
  delay(1500);
}


void setup() {
  Serial.begin(115200);
  delay(200);

  Wire.begin(SDA_PIN, SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("Echec initialisation SSD1306 - verifie le cablage I2C.");
    while (true) delay(1000);
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(40, 4);
  display.println("DJ SNAKE");
  display.setCursor(20, 30);
  display.println("Demarrage...");
  display.display();
  delay(1000);

  connectWiFi();
  tcpServer.begin();
  Serial.println("Serveur TCP demarre, en attente du script Python...");

  randomSeed(analogRead(0));
  resetGame();
}

void loop() {
  if (!tcpClient || !tcpClient.connected()) {
    WiFiClient newClient = tcpServer.available();
    if (newClient) {
      tcpClient = newClient;
      Serial.println("PC connecte.");
    }
  }

  if (tcpClient && tcpClient.connected()) {
    while (tcpClient.available() > 0) {
      char c = tcpClient.read();
      Serial.print("Recu du PC: '");
      Serial.print(c);
      Serial.print("' (code ");
      Serial.print((int)c);
      Serial.println(")");
      handleDirectionInput(c);
    }
  }

  unsigned long now = millis();
  if (now - lastMoveTime >= MOVE_INTERVAL_MS) {
    lastMoveTime = now;
    updateGame();
    drawGame();
  }
}