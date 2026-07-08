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
         'X' -> valider / reset

  MENU / MODES DE JEU :
    Au demarrage (et apres chaque Game Over), un menu s'affiche pour choisir
    le mode :
      - Solo : terrain vide, juste les bords.
      - Murs : des blocs de murs (2-3 cases) sont places aleatoirement sur
        le terrain. Ils sont dessines en CONTOUR (pas remplis) pour bien les
        distinguer du serpent, qui lui est dessine en PLEIN.
    Dans le menu : Haut/Bas pour choisir, 'X' (touche 'r' cote PC) pour valider.
*/

#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <string.h>

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

// ---------- Modes de jeu ----------
#define MODE_SOLO  0
#define MODE_WALLS 1
const uint8_t WALL_BLOCK_COUNT = 6;

enum GameState { STATE_MENU, STATE_PLAYING };
GameState gameState = STATE_MENU;
uint8_t menuSelection = MODE_SOLO;
uint8_t gameMode = MODE_SOLO;

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

bool isWall[GRID_COLS][GRID_ROWS];


void clearWalls() {
  memset(isWall, 0, sizeof(isWall));
}

void generateWalls() {
  clearWalls();
  int8_t centerX = GRID_COLS / 2;
  int8_t centerY = GRID_ROWS / 2;

  uint8_t blocksPlaced = 0;
  uint16_t attempts = 0;
  while (blocksPlaced < WALL_BLOCK_COUNT && attempts < 300) {
    attempts++;
    bool horizontal = random(0, 2) == 0;
    uint8_t blockLen = random(2, 4); // 2 ou 3 cases
    uint8_t startX, startY;

    if (horizontal) {
      if (GRID_COLS <= blockLen) continue;
      startX = random(0, GRID_COLS - blockLen);
      startY = random(0, GRID_ROWS);
    } else {
      if (GRID_ROWS <= blockLen) continue;
      startX = random(0, GRID_COLS);
      startY = random(0, GRID_ROWS - blockLen);
    }

    // verifie que la zone est libre et assez loin du point de depart du serpent
    bool free = true;
    for (uint8_t i = 0; i < blockLen; i++) {
      uint8_t cx = horizontal ? (startX + i) : startX;
      uint8_t cy = horizontal ? startY : (startY + i);
      if (isWall[cx][cy]) { free = false; break; }
      if (abs((int)cx - centerX) <= 3 && abs((int)cy - centerY) <= 2) { free = false; break; }
    }
    if (!free) continue;

    for (uint8_t i = 0; i < blockLen; i++) {
      uint8_t cx = horizontal ? (startX + i) : startX;
      uint8_t cy = horizontal ? startY : (startY + i);
      isWall[cx][cy] = true;
    }
    blocksPlaced++;
  }
}


void placeFood() {
  bool invalid;
  do {
    invalid = false;
    food.x = random(0, GRID_COLS);
    food.y = random(0, GRID_ROWS);
    for (uint16_t i = 0; i < snakeLength; i++) {
      if (snake[i].x == food.x && snake[i].y == food.y) {
        invalid = true;
        break;
      }
    }
    if (!invalid && gameMode == MODE_WALLS && isWall[food.x][food.y]) {
      invalid = true;
    }
  } while (invalid);
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

// demarre une partie dans le mode actuellement selectionne dans le menu
void startGameFromMenu() {
  gameMode = menuSelection;
  if (gameMode == MODE_WALLS) {
    generateWalls();
  } else {
    clearWalls();
  }
  resetGame();
  gameState = STATE_PLAYING;
}

void handleDirectionInput(char c) {
  if (gameState == STATE_MENU) {
    switch (c) {
      case 'U': menuSelection = MODE_SOLO; break;
      case 'D': menuSelection = MODE_WALLS; break;
      case 'X': startGameFromMenu(); break;
      default: break;
    }
    return;
  }

  if (gameOver) {
    if (c == 'X') {
      gameState = STATE_MENU;
    }
    return;
  }

  Direction requested;
  switch (c) {
    case 'U': requested = DIR_UP; break;
    case 'D': requested = DIR_DOWN; break;
    case 'L': requested = DIR_LEFT; break;
    case 'R': requested = DIR_RIGHT; break;
    case 'X':
      // reset manuel en cours de partie, sans repasser par le menu
      resetGame();
      if (gameMode == MODE_WALLS) generateWalls();
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

  if (newHead.x < 0 || newHead.x >= GRID_COLS || newHead.y < 0 || newHead.y >= GRID_ROWS) {
    gameOver = true;
    Serial.println("Game Over (bord) - score final: " + String(score));
    return;
  }

  if (gameMode == MODE_WALLS && isWall[newHead.x][newHead.y]) {
    gameOver = true;
    Serial.println("Game Over (mur obstacle) - score final: " + String(score));
    return;
  }

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

void drawTitle() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(40, 4);
  display.print("DJ SNAKE");
}

void drawMenu() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(10, 20);
  display.print(menuSelection == MODE_SOLO ? "> " : "  ");
  display.println("Solo");

  display.setCursor(10, 32);
  display.print(menuSelection == MODE_WALLS ? "> " : "  ");
  display.println("Murs");

  display.setCursor(2, 46);
  display.println("Haut/Bas: choisir");
  display.setCursor(2, 56);
  display.println("R: valider");
}

void drawGame() {
  display.clearDisplay();
  drawTitle();

  if (gameState == STATE_MENU) {
    drawMenu();
    display.display();
    return;
  }

  if (gameOver) {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(20, 20);
    display.println("GAME OVER");
    display.setCursor(15, 35);
    display.println("Score: " + String(score));
    display.setCursor(5, 50);
    display.println("Touche R = menu");
    display.display();
    return;
  }

  // murs (mode "Murs" uniquement) : dessines en CONTOUR pour les distinguer
  // du serpent, qui lui est dessine en PLEIN
  if (gameMode == MODE_WALLS) {
    for (uint8_t x = 0; x < GRID_COLS; x++) {
      for (uint8_t y = 0; y < GRID_ROWS; y++) {
        if (isWall[x][y]) {
          display.drawRect(x * CELL_SIZE, PLAY_Y_OFFSET + y * CELL_SIZE, CELL_SIZE, CELL_SIZE, SSD1306_WHITE);
        }
      }
    }
  }

  display.fillRect(food.x * CELL_SIZE, PLAY_Y_OFFSET + food.y * CELL_SIZE, CELL_SIZE, CELL_SIZE, SSD1306_WHITE);

  for (uint16_t i = 0; i < snakeLength; i++) {
    display.fillRect(snake[i].x * CELL_SIZE, PLAY_Y_OFFSET + snake[i].y * CELL_SIZE, CELL_SIZE - 1, CELL_SIZE - 1, SSD1306_WHITE);
  }

  display.display();
}

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

  // init I2C sur les broches reelles (important sur ESP32-S3, dont les
  // broches I2C par defaut ne correspondent pas forcement a ton cablage)
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

  // on demarre sur le menu de selection du mode, pas directement en jeu
  clearWalls();
  gameState = STATE_MENU;
  menuSelection = MODE_SOLO;
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
    if (gameState == STATE_PLAYING) {
      updateGame();
    }
    drawGame();
  }
}