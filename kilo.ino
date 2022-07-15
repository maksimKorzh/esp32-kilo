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

#define KILO_VERSION "0.0.1"

enum editorKey {
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN,
  ENTER_KEY,
  BACKSPACE_KEY,
  ESCAPE_KEY
};

typedef struct erow {
  int size;
  char *chars;
} erow;

struct editorConfig {
  int cx, cy;
  int screenrows;
  int screencols;
  int numrows;
  erow *row;
}; struct editorConfig E;

struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT {NULL, 0}

void editorAppendRow(char *s, size_t len) {
  E.row = (erow *)realloc(E.row, sizeof(erow) * (E.numrows + 1));
  int at = E.numrows;
  E.row[at].size = len;
  E.row[at].chars = (char *)malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';
  E.numrows++;
}

void editorOpen() {
  char *line1 = "Hello, world!";
  editorAppendRow(line1, 13);
  char *line2 = "This is an epic line 2";
  editorAppendRow(line2, 22);
  char *line3 = "And this one is the third and the last one for now...";
  editorAppendRow(line3, 53);
}

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
  for (y = 0; y < E.screenrows; y++) {
    if (y >= E.numrows) {
      if (E.numrows == 0 && y == E.screenrows / 3) {
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
          "Kilo editor -- version %s", KILO_VERSION);
        if (welcomelen > E.screencols) welcomelen = E.screencols;
        int padding = (E.screencols - welcomelen) / 2;
        if (padding) {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--) abAppend(ab, " ", 1);
        abAppend(ab, welcome, welcomelen);
      } else {
        abAppend(ab, "~", 1);
      }
    } else {
      int len = E.row[y].size;
      if (len > E.screencols) len = E.screencols;
      abAppend(ab, E.row[y].chars, len);
    }
    abAppend(ab, "\x1b[K", 3);
    if (y < E.screenrows - 1) {
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
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
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

void editorMoveCursor(int key) {
  switch (key) {
    case ARROW_LEFT:
      if (E.cx != 0) {
        E.cx--;
      }
      break;
    case ARROW_RIGHT:
      if (E.cx != E.screencols - 1) {
        E.cx++;
      }
      break;
    case ARROW_UP:
      if (E.cy != 0) {
        E.cy--;
      }
      break;
    case ARROW_DOWN:
      if (E.cy != E.screenrows - 1) {
        E.cy++;
      }
      break;
  }
}

int editorReadKey() {
  auto keyboard = PS2Controller.keyboard();
  while (!keyboard->virtualKeyAvailable()) {/* wait for a key press */}
  VirtualKeyItem item;
  if (keyboard->getNextVirtualKey(&item)) {
  
    // debug
    /*if (item.down) {
      xprintf("Scan codes: ");
      xprintf("0x%02X 0x%02X 0x%02X\n\r", item.scancode[0], item.scancode[1], item.scancode[2]);
    }*/
    
    if (item.down) {
      if (item.scancode[0] == 0xE0) {
        switch (item.scancode[1]) {
          case 0x6B: return ARROW_LEFT;
          case 0x74: return ARROW_RIGHT;
          case 0x75: return ARROW_UP;
          case 0x72: return ARROW_DOWN;
          case 0x71: return DEL_KEY;
          case 0x6C: return HOME_KEY;
          case 0x69: return END_KEY;
          case 0x7D: return PAGE_UP;
          case 0x7A: return PAGE_DOWN;
        }
      } else {
        switch (item.scancode[0]) {
          case 0x76: return ESCAPE_KEY;
          case 0x5A: return ENTER_KEY;
          case 0x66: return BACKSPACE_KEY;
          default: return item.ASCII;
        }
      }
    }
  }

}

void editorProcessKeypress() {
  int c = editorReadKey();
  //xprintf("Key: %d %c\r\n", c, c);
  switch (c) {
    case HOME_KEY: E.cx = 0; break;
    case END_KEY: E.cx = E.screencols - 1; break;
    case PAGE_UP:
    case PAGE_DOWN:
      {
        int times = E.screencols;
        while (times--)
          editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
      }
      break;
    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      editorMoveCursor(c);
      break;
  }
}

void initEditor() {
  E.cx = 0;
  E.cy = 0;
  E.screenrows = 25;
  E.screencols = 80;
  E.numrows = 0;
  E.row = NULL;
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
  Terminal.enableCursor(true);
  
  // init SPIFFS
  if(!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)){
    xprintf("SPIFFS Mount Failed");
    return;
  }
  
  // init text editor
  initEditor();
  editorOpen();
}

void loop() {
  editorRefreshScreen();
  editorProcessKeypress();
}


