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
         Fleches (majuscules) -> 'U'/'D'/'L'/'R' : Joueur 2
         ZQSD (minuscules)    -> 'u'/'d'/'l'/'r' : Joueur 1 (seul joueur en Solo/Murs)
         'X' -> valider / reset

  MENU / MODES DE JEU :
    Au demarrage (et apres chaque Game Over), un menu en 2 etapes s'affiche
    (Haut/Bas pour naviguer, 'X' pour valider) :
      1. Solo ou Multijoueur :
         - Solo : un seul serpent.
         - Multijoueur : 2 serpents en meme temps. Joueur 1 (ZQSD) est
           dessine en gros carres pleins, Joueur 2 (fleches) en petits points
           centres, pour rester distinguables sur cet ecran monochrome.
      2. Classique ou Murs :
         - Classique : terrain vide, juste les bords.
         - Murs : des blocs de murs (2-3 cases) sont places aleatoirement sur
           le terrain. Ils sont dessines en CONTOUR (pas remplis) pour bien
           les distinguer du/des serpent(s), dessine(s) en PLEIN.
    Ces 2 choix se combinent (ex: Multijoueur + Murs).
*/

#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <string.h>

const char* WIFI_SSID = "decode-etudiants";
const char* WIFI_PASSWORD = "learnByDoing25!";
const uint16_t TCP_PORT = 3333; // Port pour connexion au code Pyhton

WiFiServer tcpServer(TCP_PORT);
WiFiClient tcpClient;

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C 


#define SDA_PIN 17
#define SCL_PIN 18

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Partie titre du jeu (taille en px)
const uint8_t TITLE_HEIGHT = 16;
const uint8_t PLAY_Y_OFFSET = TITLE_HEIGHT;

// Partie jeu (taille en px)
const uint8_t CELL_SIZE = 4;
const uint8_t GRID_COLS = SCREEN_WIDTH / CELL_SIZE;
const uint8_t GRID_ROWS = (SCREEN_HEIGHT - TITLE_HEIGHT) / CELL_SIZE;
const uint16_t MAX_SNAKE_LENGTH = GRID_COLS * GRID_ROWS;
const unsigned long MOVE_INTERVAL_MS = 150;

// Nombre de blocs max dans le mode murs
const uint8_t WALL_BLOCK_COUNT = 6;

// Menu mode de jeu
enum GameState { STATE_MENU_PLAYERS, STATE_MENU_TERRAIN, STATE_PLAYING };
GameState gameState = STATE_MENU_PLAYERS;
uint8_t menuPlayerSelection = 0;
uint8_t menuTerrainSelection = 0;
bool twoPlayers = false;
bool useWalls = false;

enum Direction { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT };

struct Point {
  int8_t x;
  int8_t y;
};

// Serpent 1
Point snake[MAX_SNAKE_LENGTH];
uint16_t snakeLength;
Direction currentDirection;
Direction pendingDirection;
Point food;
bool gameOver = false;
unsigned long lastMoveTime = 0;
uint16_t score = 0;

// Serpent 2 en cas de multijoueur
Point snake2[MAX_SNAKE_LENGTH];
uint16_t snake2Length;
Direction currentDirection2;
Direction pendingDirection2;
uint16_t score2 = 0;
bool player1Dead = false;
bool player2Dead = false;

bool isWall[GRID_COLS][GRID_ROWS];


void clearWalls() {
  memset(isWall, 0, sizeof(isWall));
}

// Vérifiaction qu'il n'y a pas un mur sur le serpent ou trop proche du serpent
bool isTooCloseToStart(uint8_t cx, uint8_t cy) {
  if (twoPlayers) {
    int8_t x1 = GRID_COLS / 4;
    int8_t y1 = GRID_ROWS / 3;
    int8_t x2 = GRID_COLS * 3 / 4;
    int8_t y2 = (GRID_ROWS * 2) / 3;
    if (abs((int)cx - x1) <= 3 && abs((int)cy - y1) <= 2) return true;
    if (abs((int)cx - x2) <= 3 && abs((int)cy - y2) <= 2) return true;
    return false;
  }
  int8_t centerX = GRID_COLS / 2;
  int8_t centerY = GRID_ROWS / 2;
  return abs((int)cx - centerX) <= 3 && abs((int)cy - centerY) <= 2;
}

void generateWalls() {
  clearWalls();

// On pose des murs de 2 ou 3 cas aléatoirement sur la map
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

    bool free = true;
    for (uint8_t i = 0; i < blockLen; i++) {
      uint8_t cx = horizontal ? (startX + i) : startX;
      uint8_t cy = horizontal ? startY : (startY + i);
      if (isWall[cx][cy]) { free = false; break; }
      if (isTooCloseToStart(cx, cy)) { free = false; break; }
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

// La "pomme" apparait aléatoirement sur la map
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
    if (!invalid && twoPlayers) {
      for (uint16_t i = 0; i < snake2Length; i++) {
        if (snake2[i].x == food.x && snake2[i].y == food.y) {
          invalid = true;
          break;
        }
      }
    }
    if (!invalid && useWalls && isWall[food.x][food.y]) {
      invalid = true;
    }
  } while (invalid);
}

// La partie recommence
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

// La partie recommence pour le multi
void resetTwoPlayerGame() {
  uint8_t row1 = GRID_ROWS / 3;
  uint8_t row2 = (GRID_ROWS * 2) / 3;

  snakeLength = 3;
  snake[0] = { (int8_t)(GRID_COLS / 4 + 2), (int8_t)row1 };
  snake[1] = { (int8_t)(GRID_COLS / 4 + 1), (int8_t)row1 };
  snake[2] = { (int8_t)(GRID_COLS / 4),     (int8_t)row1 };
  currentDirection = DIR_RIGHT;
  pendingDirection = DIR_RIGHT;

  snake2Length = 3;
  snake2[0] = { (int8_t)(GRID_COLS * 3 / 4 - 2), (int8_t)row2 };
  snake2[1] = { (int8_t)(GRID_COLS * 3 / 4 - 1), (int8_t)row2 };
  snake2[2] = { (int8_t)(GRID_COLS * 3 / 4),     (int8_t)row2 };
  currentDirection2 = DIR_LEFT;
  pendingDirection2 = DIR_LEFT;

  score = 0;
  score2 = 0;
  player1Dead = false;
  player2Dead = false;
  gameOver = false;
  placeFood();
  lastMoveTime = millis();
}

// Pass de demi tour possible pour le snake
bool isOppositeDirection(Direction a, Direction b) {
  return (a == DIR_UP && b == DIR_DOWN) ||
         (a == DIR_DOWN && b == DIR_UP) ||
         (a == DIR_LEFT && b == DIR_RIGHT) ||
         (a == DIR_RIGHT && b == DIR_LEFT);
}

// Démarrage de la partie une fois avoir choisi le mode
void startGameFromMenu() {
  twoPlayers = (menuPlayerSelection == 1);
  useWalls = (menuTerrainSelection == 1);
  if (useWalls) {
    generateWalls();
  } else {
    clearWalls();
  }
  if (twoPlayers) {
    resetTwoPlayerGame();
  } else {
    resetGame();
  }
  gameState = STATE_PLAYING;
}

// Gestion des touches envoyées
void handleDirectionInput(char c) {
  bool isReset = (c == 'X' || c == 'x');

  // Ici on est dans le menu pour choisir si solo ou multi
  if (gameState == STATE_MENU_PLAYERS) {
    switch (c) {
      case 'U': case 'u': menuPlayerSelection = 0; break;
      case 'D': case 'd': menuPlayerSelection = 1; break;
      default: if (isReset) gameState = STATE_MENU_TERRAIN; break;
    }
    return;
  }

  // Ici on choisit la map, soit classique soit murs
  if (gameState == STATE_MENU_TERRAIN) {
    switch (c) {
      case 'U': case 'u': menuTerrainSelection = 0; break;
      case 'D': case 'd': menuTerrainSelection = 1; break;
      default: if (isReset) startGameFromMenu(); break;
    }
    return;
  }

  // Si un joueur perd, on retourne au menu
  if (gameOver) {
    if (isReset) {
      gameState = STATE_MENU_PLAYERS;
    }
    return;
  }

  if (isReset) {
    // reset manuel en cours de partie, sans repasser par le menu
    if (twoPlayers) {
      resetTwoPlayerGame();
    } else {
      resetGame();
    }
    if (useWalls) generateWalls();
    return;
  }

  if (twoPlayers) {
    // Si multijoueur, joueur 1 joue avec ZQSD, et le joueur 2 avec les flèches
    switch (c) {
      case 'u': if (!isOppositeDirection(DIR_UP, currentDirection))     pendingDirection = DIR_UP;    break;
      case 'd': if (!isOppositeDirection(DIR_DOWN, currentDirection))   pendingDirection = DIR_DOWN;  break;
      case 'l': if (!isOppositeDirection(DIR_LEFT, currentDirection))  pendingDirection = DIR_LEFT;  break;
      case 'r': if (!isOppositeDirection(DIR_RIGHT, currentDirection)) pendingDirection = DIR_RIGHT; break;
      case 'U': if (!isOppositeDirection(DIR_UP, currentDirection2))    pendingDirection2 = DIR_UP;    break;
      case 'D': if (!isOppositeDirection(DIR_DOWN, currentDirection2))  pendingDirection2 = DIR_DOWN;  break;
      case 'L': if (!isOppositeDirection(DIR_LEFT, currentDirection2))  pendingDirection2 = DIR_LEFT;  break;
      case 'R': if (!isOppositeDirection(DIR_RIGHT, currentDirection2)) pendingDirection2 = DIR_RIGHT; break;
      default: break;
    }
    return;
  }

  // Joeuur solo, il peut utiliser ZQSD ou les flèches
  Direction requested;
  switch (c) {
    case 'U': case 'u': requested = DIR_UP; break;
    case 'D': case 'd': requested = DIR_DOWN; break;
    case 'L': case 'l': requested = DIR_LEFT; break;
    case 'R': case 'r': requested = DIR_RIGHT; break;
    default: return;
  }
  // le serpent ne peut pas faire demi tour
  if (!isOppositeDirection(requested, currentDirection)) {
    pendingDirection = requested;
  }
}

void updateGame() {
  if (gameOver) return;

  if (twoPlayers) {
    updateTwoPlayerGame();
  } else {
    updateSoloGame();
  }
}

void updateSoloGame() {
  currentDirection = pendingDirection;

  Point newHead = snake[0];
  switch (currentDirection) {
    case DIR_UP:    newHead.y--; break;
    case DIR_DOWN:  newHead.y++; break;
    case DIR_LEFT:  newHead.x--; break;
    case DIR_RIGHT: newHead.x++; break;
  }

  // Si on touche le bord de la map, on perd
  if (newHead.x < 0 || newHead.x >= GRID_COLS || newHead.y < 0 || newHead.y >= GRID_ROWS) {
    gameOver = true;
    Serial.println("Game Over (bord) - score final: " + String(score));
    return;
  }

  // Si on touche un mur, on perd
  if (useWalls && isWall[newHead.x][newHead.y]) {
    gameOver = true;
    Serial.println("Game Over (mur obstacle) - score final: " + String(score));
    return;
  }

  // Si on touche son propre serpend, on perd
  for (uint16_t i = 0; i < snakeLength; i++) {
    if (snake[i].x == newHead.x && snake[i].y == newHead.y) {
      gameOver = true;
      Serial.println("Game Over (collision) - score final: " + String(score));
      return;
    }
  }

  bool ateFood = (newHead.x == food.x && newHead.y == food.y);

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

// Multi les deux serpent avancent en meme temps
void updateTwoPlayerGame() {
  currentDirection = pendingDirection;
  currentDirection2 = pendingDirection2;

  Point newHead1 = snake[0];
  if (!player1Dead) {
    switch (currentDirection) {
      case DIR_UP:    newHead1.y--; break;
      case DIR_DOWN:  newHead1.y++; break;
      case DIR_LEFT:  newHead1.x--; break;
      case DIR_RIGHT: newHead1.x++; break;
    }
  }

  Point newHead2 = snake2[0];
  if (!player2Dead) {
    switch (currentDirection2) {
      case DIR_UP:    newHead2.y--; break;
      case DIR_DOWN:  newHead2.y++; break;
      case DIR_LEFT:  newHead2.x--; break;
      case DIR_RIGHT: newHead2.x++; break;
    }
  }

  // Si on touche le bord de la map, on perd
  if (!player1Dead && (newHead1.x < 0 || newHead1.x >= GRID_COLS || newHead1.y < 0 || newHead1.y >= GRID_ROWS)) {
    player1Dead = true;
  }
  if (!player2Dead && (newHead2.x < 0 || newHead2.x >= GRID_COLS || newHead2.y < 0 || newHead2.y >= GRID_ROWS)) {
    player2Dead = true;
  }

  // Si on touche un mur, on perd
  if (!player1Dead && useWalls && isWall[newHead1.x][newHead1.y]) {
    player1Dead = true;
  }
  if (!player2Dead && useWalls && isWall[newHead2.x][newHead2.y]) {
    player2Dead = true;
  }

  // Si on touche le serpent adverse, on perd
  if (!player1Dead && !player2Dead && newHead1.x == newHead2.x && newHead1.y == newHead2.y) {
    player1Dead = true;
    player2Dead = true;
  }

  if (!player1Dead) {
    for (uint16_t i = 0; i < snakeLength; i++) {
      if (snake[i].x == newHead1.x && snake[i].y == newHead1.y) { player1Dead = true; break; }
    }
  }
  if (!player1Dead) {
    for (uint16_t i = 0; i < snake2Length; i++) {
      if (snake2[i].x == newHead1.x && snake2[i].y == newHead1.y) { player1Dead = true; break; }
    }
  }
  if (!player2Dead) {
    for (uint16_t i = 0; i < snake2Length; i++) {
      if (snake2[i].x == newHead2.x && snake2[i].y == newHead2.y) { player2Dead = true; break; }
    }
  }
  if (!player2Dead) {
    for (uint16_t i = 0; i < snakeLength; i++) {
      if (snake[i].x == newHead2.x && snake[i].y == newHead2.y) { player2Dead = true; break; }
    }
  }

  if (!player1Dead) {
    bool ateFood = (newHead1.x == food.x && newHead1.y == food.y);
    if (ateFood && snakeLength < MAX_SNAKE_LENGTH) {
      for (uint16_t i = snakeLength; i > 0; i--) snake[i] = snake[i - 1];
      snakeLength++;
      score++;
      placeFood();
    } else {
      for (uint16_t i = snakeLength - 1; i > 0; i--) snake[i] = snake[i - 1];
    }
    snake[0] = newHead1;
  }

  if (!player2Dead) {
    bool ateFood = (newHead2.x == food.x && newHead2.y == food.y);
    if (ateFood && snake2Length < MAX_SNAKE_LENGTH) {
      for (uint16_t i = snake2Length; i > 0; i--) snake2[i] = snake2[i - 1];
      snake2Length++;
      score2++;
      placeFood();
    } else {
      for (uint16_t i = snake2Length - 1; i > 0; i--) snake2[i] = snake2[i - 1];
    }
    snake2[0] = newHead2;
  }

  if (player1Dead || player2Dead) {
    gameOver = true;
    Serial.println("Game Over - J1: " + String(score) + " / J2: " + String(score2));
  }
}


// Affichage du titre du jeu
void drawTitle() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(40, 4);
  display.print("DJ SNAKE");
}

// Affichage du menu pour choisir le nombre de joueurs
void drawPlayerMenu() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(10, 20);
  display.print(menuPlayerSelection == 0 ? "> " : "  ");
  display.println("Solo");

  display.setCursor(10, 32);
  display.print(menuPlayerSelection == 1 ? "> " : "  ");
  display.println("Multijoueur");

  display.setCursor(2, 46);
  display.println("Haut/Bas: choisir");
  display.setCursor(2, 56);
  display.println("R: valider");
}

// Affichage du menu pour choisir le mode de jeu
void drawTerrainMenu() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(10, 20);
  display.print(menuTerrainSelection == 0 ? "> " : "  ");
  display.println("Classique");

  display.setCursor(10, 32);
  display.print(menuTerrainSelection == 1 ? "> " : "  ");
  display.println("Murs");

  display.setCursor(2, 46);
  display.println("Haut/Bas: choisir");
  display.setCursor(2, 56);
  display.println("R: valider");
}

// C'est cette partie de code qui va appliquer les fonctions décrite au dessus pour afficher le bon mode de jeu sur l'écran
void drawGame() {
  display.clearDisplay();
  drawTitle();

  if (gameState == STATE_MENU_PLAYERS) {
    drawPlayerMenu();
    display.display();
    return;
  }

  if (gameState == STATE_MENU_TERRAIN) {
    drawTerrainMenu();
    display.display();
    return;
  }

  // La partie se termine on affiche le score de la partie et le gagnant si mutli
  if (gameOver) {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(20, 18);
    display.println("GAME OVER");
    if (twoPlayers) {
      display.setCursor(10, 30);
      display.println("J1:" + String(score) + "  J2:" + String(score2));
      display.setCursor(5, 42);
      // le vainqueur est celui qui est encore en vie
      if (player1Dead && player2Dead) display.println("Egalite !");
      else if (player1Dead) display.println("Joueur 2 gagne !");
      else display.println("Joueur 1 gagne !");
    } else {
      display.setCursor(15, 33);
      display.println("Score: " + String(score));
    }
    display.setCursor(5, 54);
    display.println("Touche R = menu");
    display.display();
    return;
  }

  // Les murs ne sont pas pleins pour ne pas etre confondus avec les serpents
  if (useWalls) {
    for (uint8_t x = 0; x < GRID_COLS; x++) {
      for (uint8_t y = 0; y < GRID_ROWS; y++) {
        if (isWall[x][y]) {
          display.drawRect(x * CELL_SIZE, PLAY_Y_OFFSET + y * CELL_SIZE, CELL_SIZE, CELL_SIZE, SSD1306_WHITE);
        }
      }
    }
  }

  if (twoPlayers) {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 4);
    display.print("1:" + String(score));
    display.setCursor(98, 4);
    display.print("2:" + String(score2));
  }

  display.fillRect(food.x * CELL_SIZE, PLAY_Y_OFFSET + food.y * CELL_SIZE, CELL_SIZE, CELL_SIZE, SSD1306_WHITE);

  if (twoPlayers) {
    if (!player1Dead) {
      for (uint16_t i = 0; i < snakeLength; i++) {
        display.fillRect(snake[i].x * CELL_SIZE, PLAY_Y_OFFSET + snake[i].y * CELL_SIZE, CELL_SIZE - 1, CELL_SIZE - 1, SSD1306_WHITE);
      }
    }
    if (!player2Dead) {
      for (uint16_t i = 0; i < snake2Length; i++) {
        display.fillRect(snake2[i].x * CELL_SIZE + 1, PLAY_Y_OFFSET + snake2[i].y * CELL_SIZE + 1, CELL_SIZE - 2, CELL_SIZE - 2, SSD1306_WHITE);
      }
    }
  } else {
    for (uint16_t i = 0; i < snakeLength; i++) {
      display.fillRect(snake[i].x * CELL_SIZE, PLAY_Y_OFFSET + snake[i].y * CELL_SIZE, CELL_SIZE - 1, CELL_SIZE - 1, SSD1306_WHITE);
    }
  }

  display.display();
}

// Affichage des logs de connexion sur l'écran
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

// Connexion au wifi
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

// Connexion TCP avec le script Python
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

  clearWalls();
  gameState = STATE_MENU_PLAYERS;
  menuPlayerSelection = 0;
  menuTerrainSelection = 0;
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