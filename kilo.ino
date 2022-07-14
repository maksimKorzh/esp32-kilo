/*
    KILO text editor
*/

#include "fabgl.h"
#include "SPIFFS.h"

fabgl::VGA16Controller   DisplayController;
fabgl::Terminal          Terminal;
fabgl::PS2Controller     PS2Controller;

/* You only need to format SPIFFS the first time you run a
   test or else use the SPIFFS plugin to create a partition
   https://github.com/me−no−dev/arduino−esp32fs−plugin */
#define FORMAT_SPIFFS_IF_FAILED false

const uint8_t ROWS = 25;
const uint8_t COLS = 80;
uint8_t CURX = 0;
uint8_t CURY = 0;

struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
  char *newb = (char *)realloc(ab->b, ab->len + len);
  if (newb == NULL) return;
  memcpy(&newb[ab->len], s, len);
  ab->b = newb;
  ab->len += len;
}

void abFree(struct abuf *ab) {
  free(ab->b);
}

void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < ROWS; y++) {
    if (y == ROWS / 3) {
      char welcome[80];
      int welcomelen = snprintf(welcome, sizeof(welcome),
        "Kilo editor -- version 0.0.1");
      if (welcomelen > COLS) welcomelen = COLS;
      int padding = (COLS - welcomelen) / 2;
      if (padding) {
        abAppend(ab, "~", 1);
        padding--;
      }
      while (padding--) abAppend(ab, " ", 1);
      abAppend(ab, welcome, welcomelen);
    } else {
      abAppend(ab, "~", 1);
    }
    abAppend(ab, "\x1b[K", 3);
    if (y < ROWS - 1) {
      abAppend(ab, "\r\n", 2);
    }
  }
}

void editorRefreshScreen() {
  struct abuf ab = ABUF_INIT;
  abAppend(&ab, "\x1b[?25l", 6);  // hide cursor
  abAppend(&ab, "\x1b[H", 3);     // cursor home
  editorDrawRows(&ab);
  
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", CURY + 1, CURX + 1);
  abAppend(&ab, buf, strlen(buf));
  
  abAppend(&ab, "\x1b[?25h", 6);  // show cursor
  abAppend(&ab, " ", 1);
  xwritef("%s", ab.len, ab.b);
  abFree(&ab);
}

int getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;
  //if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
    if (buf[i] == 'R') break;
    i++;
  }
  buf[i] = '\0';
  if (buf[0] != '\x1b' || buf[1] != '[') return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
  return 0;
}

void editorMoveCursor(char key) {
  switch (key) {
    case 0x6B:  // left arrow
      CURX--;
      break;
    case 0x74:  // right arrow
      CURX++;
      break;
    case 0x75:  // up arrow
      CURY--;
      break;
    case 0x72:  // down arrow
      CURY++;
      break;
  } //editorRefreshScreen();
}

char editorReadKey() {
  char c = Serial.read();
  if (c == 0xFF) {
    auto keyboard = PS2Controller.keyboard();
    if (keyboard->virtualKeyAvailable()) {
      VirtualKeyItem item;
      if (keyboard->getNextVirtualKey(&item)) {              
        if (item.down) {
          // debug
          /*xprintf("Scan codes: ");
          for (int i = 0; i < 8 && item.scancode[i] != 0; i++)
            xprintf("%02X ", item.scancode[i]);
          xprintf("%c\n", item.ASCII);*/
          // handle shortcuts
          switch (item.scancode[0]) {
            case 0x5A: xprintf("\n"); break;        // ENTER
            
            
          }
          
          switch (item.scancode[1]) {
            case 0x6B:  // left arrow
            case 0x74:  // right arrow
            case 0x75:  // up arrow
            case 0x72:  // down arrow
              editorMoveCursor(item.scancode[1]);
              break;
          }
        }        
        
        return item.down ? item.ASCII : 0x00;
      }
    } return 0x00;
  } else return c;
}

void editorProcessKeypress() {
  editorReadKey();
  //while (!editorReadKey()) {};
  //Terminal.enableCursor(true); // show cursor
}

void xwritef(const char * format, int size, ...)
{
  va_list ap;
  va_start(ap, format);
  //int size = vsnprintf(nullptr, 0, format, ap) + 1;
  if (size > 0) {
    va_end(ap);
    va_start(ap, format);
    char buf[size + 1];
    vsnprintf(buf, size, format, ap);
    Serial.write(buf);
    Terminal.write(buf);
  } va_end(ap);
}


void xprintf(const char * format, ...)
{
  va_list ap;
  va_start(ap, format);
  int size = vsnprintf(nullptr, 0, format, ap) + 1;
  if (size > 0) {
    va_end(ap);
    va_start(ap, format);
    char buf[size + 1];
    vsnprintf(buf, size, format, ap);
    Serial.write(buf);
    Terminal.write(buf);
  } va_end(ap);
}

void setup() {
  // init serial port
  Serial.begin(115200);
  delay(500);  // avoid garbage into the UART
  
  // ESP32 peripherals setup
  PS2Controller.begin(PS2Preset::KeyboardPort0);
  DisplayController.begin();
  DisplayController.setResolution(VGA_640x480_60Hz);
  Terminal.begin(&DisplayController);
  //Terminal.enableCursor(true);
  
  // init SPIFFS
  if(!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)){
    xprintf("SPIFFS Mount Failed");
    return;
  }
}

void loop() {
  editorRefreshScreen();
  editorProcessKeypress();
}


