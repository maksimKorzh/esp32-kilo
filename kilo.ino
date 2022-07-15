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
#define KILO_TAB_STOP 2

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
  int rsize;
  char *chars;
  char *render;
} erow;

struct editorConfig {
  int cx, cy;
  int rx;
  int rowoff;
  int coloff;
  int screenrows;
  int screencols;
  int numrows;
  erow *row;
  char *filename;
  char statusmsg[80];
  time_t statusmsg_time;
}; struct editorConfig E;

struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT {NULL, 0}

int editorRowCxToRx(erow *row, int cx) {
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++) {
    if (row->chars[j] == '\t')
      rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
    rx++;
  }
  return rx;
}

void editorUpdateRow(erow *row) {
  int tabs = 0;
  int j;
  for (j = 0; j < row->size; j++)
    if (row->chars[j] == '\t') tabs++;
  free(row->render);
  row->render = (char *)malloc(row->size + tabs*(KILO_TAB_STOP - 1) + 1);
  int idx = 0;
  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % KILO_TAB_STOP != 0) row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;
}

void editorAppendRow(char *s, size_t len) {
  E.row = (erow *)realloc(E.row, sizeof(erow) * (E.numrows + 1));
  int at = E.numrows;
  E.row[at].size = len;
  E.row[at].chars = (char *)malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';
  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  editorUpdateRow(&E.row[at]);
  E.numrows++;
}

void editorRowInsertChar(erow *row, int at, int c) {
  if (at < 0 || at > row->size) at = row->size;
  row->chars = (char *)realloc(row->chars, row->size + 2);
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  editorUpdateRow(row);
}

void editorInsertChar(int c) {
  if (E.cy == E.numrows) {
    editorAppendRow("", 0);
  }
  editorRowInsertChar(&E.row[E.cy], E.cx, c);
  E.cx++;
}

size_t getline(char *buf, size_t *size, File *stream)
{
  char c;
  size_t count = 0;
  
  while (c != '\r') {
    if (stream->available()) {
      c = stream->read();
      buf[count] = c;
      count++;
    } else {
      if (count == 0) return -1;
      else break;
    }
  }
  
  buf[count] = '\0';
  return count;
}

void editorOpen(fs::FS &fs, const char *filename) {
  free(E.filename);
  E.filename = strdup(filename);
  
  File fp = fs.open(filename);
  if(!fp || fp.isDirectory()) {
    xprintf("− failed to open file for reading\n\r");
    return;
  }

  if (!fp) { xprintf("No file found\n\r"); return; }
  char line[200];  // 200 chars per line allowed
  size_t linecap = 0;
  ssize_t linelen;

  while ((linelen = getline(line, &linecap, &fp)) != -1) {
    while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) linelen--;    
    //xprintf("%d: %s", linelen, line);
    editorAppendRow(line, linelen);
  }
  
  fp.close();
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

void editorScroll() {
  E.rx = 0;
  if (E.cy < E.numrows) {
    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  }
  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  }
  if (E.cy >= E.rowoff + E.screenrows) {
    E.rowoff = E.cy - E.screenrows + 1;
  }
  if (E.rx < E.coloff) {
    E.coloff = E.rx;
  }
  if (E.rx >= E.coloff + E.screencols) {
    E.coloff = E.rx - E.screencols + 1;
  }
}

void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < E.screenrows; y++) {
    int filerow = y + E.rowoff;
    if (filerow >= E.numrows) {
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
      int len = E.row[filerow].rsize - E.coloff;
      if (len < 0) len = 0;
      if (len >= E.screencols) len = E.screencols - 1;
      abAppend(ab, &E.row[filerow].render[E.coloff], len);
    }
    abAppend(ab, "\x1b[K", 3);     // clear line
    abAppend(ab, "\n\r", 2);
  }
}

void editorDrawStatusBar(struct abuf *ab) {
  abAppend(ab, "\x1b[7m", 4);
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines",
    E.filename ? E.filename : "[No Name]", E.numrows);
  int rlen = snprintf(rstatus, sizeof(rstatus), "row %d, col %d",
    E.cy + 1, E.cx + 1);
  if (len > E.screencols) len = E.screencols;
  abAppend(ab, status, len);
  while (len < E.screencols) {
    if (E.screencols - len == rlen) {
      abAppend(ab, rstatus, rlen);
      break;
    } else {
      abAppend(ab, " ", 1);
      len++;
    }
  }
  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab) {
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencols) msglen = E.screencols;
  if (msglen && time(NULL) - E.statusmsg_time < 5)
    abAppend(ab, E.statusmsg, msglen);
}

void editorRefreshScreen() {
  editorScroll();
  struct abuf ab = ABUF_INIT;
  abAppend(&ab, "\x1b[?25l", 6);  // hide cursor
  abAppend(&ab, "\x1b[H", 3);     // cursor home
  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);
  editorDrawMessageBar(&ab);
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1,
                                            (E.rx - E.coloff) + 1);
  abAppend(&ab, buf, strlen(buf));
  abAppend(&ab, "\x1b[?25h", 6);  // show cursor
  Terminal.write(ab.b, ab.len);
  abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
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
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  switch (key) {
    case ARROW_LEFT:
      if (E.cx != 0) {
        E.cx--;
      } else if (E.cy > 0) {
        E.cy--;
        E.cx = E.row[E.cy].size;
      }
      break;
    case ARROW_RIGHT:
      if (row && E.cx < row->size) {
        E.cx++;
      } else if (row && E.cx == row->size) {
        E.cy++;
        E.cx = 0;
      }
      break;
    case ARROW_UP:
      if (E.cy != 0) {
        E.cy--;
      }
      break;
    case ARROW_DOWN:
      if (E.cy < E.numrows) {
        E.cy++;
      }
      break;
  }
  
  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen) {
    E.cx = rowlen;
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
    } return 0x00;
  }

}

void editorProcessKeypress() {
  int c = editorReadKey();
  if (!c) return;
  switch (c) {
    /*case CTRL_KEY('q'):
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;*/
    case HOME_KEY:
      E.cx = 0;
      break;
    case END_KEY:
      if (E.cy < E.numrows)
        E.cx = E.row[E.cy].size;
      break;
    case PAGE_UP:
    case PAGE_DOWN:
      {
        if (c == PAGE_UP) {
          E.cy = E.rowoff;
        } else if (c == PAGE_DOWN) {
          E.cy = E.rowoff + E.screenrows - 1;
          if (E.cy > E.numrows) E.cy = E.numrows;
        }
        int times = E.screenrows;
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
    default:
      editorInsertChar(c);
      break;
  }
}

void readFile(fs::FS &fs, const char * path){
   //Serial.printf("Reading file: %s\n\r", path);

   File file = fs.open(path);
   if(!file || file.isDirectory()){
       //Serial.println("− failed to open file for reading");
       return;
   }

   //Serial.println("− read from file:");
   while(file.available()){
      Serial.write(file.read());
   }
}

void writeFile(fs::FS &fs, const char * path, const char * message){
   //xprintf("Writing file: %s\n\r", path);

   File file = fs.open(path, FILE_WRITE);
   if(!file){
      //xprintf("− failed to open file for writing\n\r");
      return;
   }
   if(file.print(message)){
      //xprintf("− file written\n\r");
   }else {
      //xprintf("− frite failed\n\r");
   }
}

void initEditor() {
  E.cx = 0;
  E.cy = 0;
  E.rx = 0;
  E.rowoff = 0;
  E.coloff = 0;
  E.screenrows = 25;
  E.screencols = 80;
  E.numrows = 0;
  E.row = NULL;
  E.filename = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;
  E.screenrows -= 2;
}

void xprintf(const char * format, ...) {
  va_list ap;
  va_start(ap, format);
  int size = vsnprintf(nullptr, 0, format, ap) + 1;
  if (size > 0) {
    va_end(ap);
    va_start(ap, format);
    char buf[size + 1];
    vsnprintf(buf, size, format, ap);
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
  //Terminal.write("\x1B[?7l"); // disable line wrap
  
  // init SPIFFS
  if(!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)){
    xprintf("SPIFFS Mount Failed");
    return;
  }
  
  // init text editor
  initEditor();  
  
  //writeFile(SPIFFS, "/hello.txt", "this is a line");
  //writeFile(SPIFFS, "/hello.txt", "this is first \n\r this is line 2 \n\r this is line 3 \n\r");
  
  //writeFile(SPIFFS, "/hello.txt", "this is first \n\r this is line 2 \n\rhello 00000000000000000123456789 123456789 123456789 123456789 123456789 123456789 123456789 12345678E    this one is really looong!!!!!\n\r\n\r this is line 4 \n\r this is line 5 \n\r");
  //writeFile(SPIFFS, "/hello.txt", "this is first \n\r this is  \t\t\t   line 2 \n\r123456789 123456789 123456789 123456789 123456789 123456789 123456789 12345678E\n\r this is line 4 \n\r this is line 5 \n\r this is line 6 \n\r this is line 7 \n\r this is line 8 \n\r this is line 9 \n\r this is line 10 \n\r this is line 11 \n\r this is line 12 \n\r this is line 13 \n\r this is line 14 \n\r this is line 15 \n\r this is line 16 \n\r this is line 17 \n\r this is line 18 \n\r this is line 19 \n\r");
  
  writeFile(SPIFFS, "/hello.txt", "this is first \n\r this is line 2 \n\rhello 00000000000000000123456789 123456789 123456789 123456789 123456789 123456789 123456789 12345678E    this one is really looong!!!!!\n\r\n\r this is line 4 \n\r this is line 5 \n\r this is line 6 \n\r this is line 7 \n\r this is line 8 \n\r this is line 9 \n\r this is line 10 \n\r this is line 11 \n\r this is line 12 \n\r this is line 13 \n\r this is line 14 \n\r this is line 15 \n\r this is line 16 \n\r this is line 17 \n\r this is line 18 \n\r this is line 19 \n\r this is line 20 \n\r this is line 21 \n\r this is line 22 \n\r this is line 23 \n\r this is line 24 \n\r this is line 25 \n\r this is line 26 \n\r this is line 27 \n\r this is line  \n\r this is line 28 \n\r this is line 30 \n\r");
  editorOpen(SPIFFS, "/hello.txt");
  editorSetStatusMessage("                                HELP: Ctrl-Q = quit");
  
  
  /*Terminal.write("\x1b[?25l");  //hide
  Terminal.write("\x1b[H");     //home

  Terminal.write("\x1b[?7l"); // disable line wrap
  Terminal.write("this is first \x1b[m\n\r this is line 2 \x1b[m\n\rhello 00000000000000000123456789 123456789 123456789 123456789 123456789 123456789 123456789 12345678E    this one is really looong!!!!!\x1b[m\n\r\n\r this is line 4 \x1b[m\n\r this is line 5 \x1b[m\n\r");
  
  //Terminal.write("\x1b[m");     // clear line
  Terminal.write("\x1b[?25h");  // show*/
}

void loop() {
  editorRefreshScreen();
  editorProcessKeypress();
}


