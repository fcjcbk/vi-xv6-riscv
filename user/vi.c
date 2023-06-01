#include "kernel/fcntl.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/re.h"
#include "user/user.h"

// screen
//  statusbar is append after SCREEN_HEIGHT screen
//  actual screen height is SCREEN_HEIGHT+1
#define SCREEN_WIDTH 30
#define SCREEN_HEIGHT 20

// line buffer length
#define LINE_BUFFER_LENGTH 128

// mode
#define MODE_NORMAL 1
#define MODE_INSERT 2

// statusbar
#define STATUSBAR_VISIBLE 1
#define STATUSBAR_HIDE 2
#define STATUSBAR_MESSAGE_LENGTH 64
#define STATUSBAR_MESSAGE_START 15
#define FIND_STR_LENGTH 31

// keycode
#define KEYCODE_ESC 0x1b
#define KEYCODE_LF 0x0A
#define KEYCODE_CR 0x0D
#define KEYCODE_DEL 0x7f
#define KEYCODE_AT 0x40
#define KEYCODE_DELETE 127
#define KEYCODE_TAB 0x9
#define KEYCODE_CTRL_L 0x0c

// esc sequence
#define term_cursor_location(x, y) fprintf(stdout, "\033[%d;%dH", y, x)

#define stdout 1
#define stdin 0
#define NULL 0

#define KEYWORD_NUM 15

// globals
int mode;
int command;
int quit_flg;
// 1表示有更改要重画屏幕，0表示不用
int is_change = 1;

int is_hightlight = 1;

struct linebuffer {
  char *buf;
  int size;
  struct linebuffer *prev;
  struct linebuffer *next;
  int dirty;  // 0表示为未修改，1表示当前行要重新渲染，2表示当前行要重新渲染且接下来的行要重新渲染
} linebuffer_head, linebuffer_tail;

struct cursor {
  int x;
  int y;
  struct linebuffer *linebuffer;
} cursor;

struct statusbar {
  int visibility;
  char mode[16];
  char msg[STATUSBAR_MESSAGE_LENGTH];
  int msglength;
} statusbar;

struct screen {
  int line;
  struct linebuffer *upperline;
} screen;

struct linebuffer *last_delete_line = 0;

enum colorenum {
  WHITE,           // 30
  RED,             // 31
  GREEN,           // 32
  YELLOW,          // 33
  BLUE,            // 34
  MAGENTA,         // 35
  CYAN,            // 36
  BRIGHT_BLACK,    // 90
  BRIGHT_RED,      // 91
  BRIGHT_GREEN,    // 92
  BRIGHT_YELLOW,   // 93
  BRIGHT_BLUE,     // 94
  BRIGHT_MAGENTA,  // 95
  BRIGHT_CYAN,     // 96
  BRIGHT_WHITE,    // 97
};

#define COLOR_clear "\e[0m"
char colors[][9] = {"\e[1;37m", "\e[1;31m", "\e[1;32m", "\e[1;33m", "\e[1;34m",
                    "\e[1;35m", "\e[1;36m", "\e[1;90m", "\e[1;91m", "\e[1;92m",
                    "\e[1;93m", "\e[1;94m", "\e[1;95m", "\e[1;96m", "\e[1;97m"};

struct keywrod {
  enum colorenum color;
  char *word;
  int flag;
};

enum colorenum word_color[LINE_BUFFER_LENGTH];

// struct keywrod keywords[] = {{RED, "("},
//                              {RED, ")"},
//                              {GREEN, "{"},
//                              {GREEN, "}"},
//                              {YELLOW, "["},
//                              {YELLOW, "]"},
//                              {BLUE, "^if$"},
//                              {BLUE, "else"},
//                              {BLUE, "^while$"},
//                              {MAGENTA, "^for$"},
//                              {MAGENTA, "#include"},
//                              {CYAN, "^int$"},
//                              {BRIGHT_RED, "printf"},
//                              {BRIGHT_GREEN, "^char$"},
//                              {BLUE, "^break$"}};

struct keywrod keywords[] = {{RED, "(", 0},
                             {RED, ")", 0},
                             {GREEN, "{", 0},
                             {GREEN, "}", 0},
                             {YELLOW, "[", 0},
                             {YELLOW, "]", 0},
                             {BLUE, "\\b?if\\b?", 1},
                             {BLUE, "\\b?else\\b?", 1},
                             {BLUE, "\\b?while\\b?", 1},
                             {MAGENTA, "\\b?for\\b?", 1},
                             {MAGENTA, "\\b?include\\b?", 1},
                             {CYAN, "\\b?int\\b?", 1},
                             {BRIGHT_RED, "\\b?double\\b?", 1},
                             {BRIGHT_GREEN, "\\b?printf\\b?", 1},
                             {BLUE, "\\b?break\\b?", 1}};

char find_str[FIND_STR_LENGTH + 1];

char inputfilename[64];
char outputfilename[64];

// protos
void save();
void load();
void quit();

// screen
void screen_init() {
  screen.upperline = linebuffer_head.next;
  screen.line = 1;
}

void screen_up() {
  if (screen.upperline->prev == &linebuffer_head) return;
  screen.upperline = screen.upperline->prev;
  screen.line--;
  is_change = 1;
}

void screen_down() {
  if (screen.upperline->next == &linebuffer_tail) return;
  screen.upperline = screen.upperline->next;
  screen.line++;
  is_change = 1;
}

int is_screen_up() { return screen.upperline->prev == cursor.linebuffer; }

int is_screen_down() {
  int i;
  struct linebuffer *lbp;

  lbp = screen.upperline;
  for (i = 1; i < SCREEN_HEIGHT; i++) {
    lbp = lbp->next;
  }
  return lbp->next == cursor.linebuffer;
}

struct linebuffer *screen_top() {
  int i;
  struct linebuffer *top;

  top = &linebuffer_head;
  for (i = 0; i < screen.line; i++) {
    top = top->next;
  }
  return top;
}

// cursor
void cursor_init(struct linebuffer *lbp) {
  cursor.x = 0;
  cursor.y = 1;
  cursor.linebuffer = lbp;
}

void terminal_cursor_update() {
  if (command) {
    term_cursor_location(STATUSBAR_MESSAGE_START + statusbar.msglength,
                         SCREEN_HEIGHT + 1);
  } else {
    term_cursor_location(cursor.x + 1, cursor.y - screen.line + 1);
  }
}

void cursor_up() {
  if (cursor.linebuffer->prev == &linebuffer_head) return;
  cursor.linebuffer = cursor.linebuffer->prev;
  cursor.y--;
  if (cursor.y == 0) {
    exit(-1);
  }
  if (cursor.x > cursor.linebuffer->size) cursor.x = cursor.linebuffer->size;

  if (is_screen_up()) screen_up();
}

void cursor_down() {
  if (cursor.linebuffer->next == &linebuffer_tail) return;
  cursor.linebuffer = cursor.linebuffer->next;
  cursor.y++;
  if (cursor.x > cursor.linebuffer->size) cursor.x = cursor.linebuffer->size;

  if (is_screen_down()) screen_down();
}

void cursor_left() {
  if (cursor.x > 0) cursor.x--;
}

void cursor_right() {
  if (cursor.x < cursor.linebuffer->size) cursor.x++;
}

void printline(struct linebuffer *lbp) {
  lbp->dirty = 0;

  int i = 0;
  char *p = lbp->buf;

  memset(word_color, 0, sizeof(word_color));

  while (i < lbp->size) {
    while (i < lbp->size && p[i] == ' ') {
      printf(" ");
      i++;
    }

    if (i >= lbp->size) {
      break;
    }
    int j = i;
    int size = 0;
    while (j < lbp->size && p[j] != ' ') {
      j++;
    }
    size = j - i;
    char *str = (char *)malloc(size + 1);
    safestrcpy(str, p + i, size + 1);

    int rematch_length = -1;
    for (int k = 0; k < KEYWORD_NUM; k++) {
      int idx = re_match(keywords[k].word, str, &rematch_length);
      if (idx != -1) {
        int w_index = 0;
        while (w_index < rematch_length) {
          word_color[idx + w_index] = (int)keywords[k].color;
          w_index++;
        }
      }
    }
    for (int k = 0; k < strlen(str); k++) {
      printf("%s%c%s", colors[word_color[k]], str[k], COLOR_clear);
    }
    memset(word_color, 0, size * sizeof(int));
    free(str);
    i = j;
  }
  printf("\n");
}

int matchwordedge(char c)
{
  return !isdigit(c) && !isalpha(c); // 单词边界，非数字或字符
}

void printline1(struct linebuffer *lbp) {
  lbp->dirty = 0;

  char *p = lbp->buf;

  memset(word_color, 0, sizeof(word_color));

  for (int k = 0; k < KEYWORD_NUM; k++) {
    int idx;
    int len;
    int i = 0;
    re_t regex = re_compile(keywords[k].word);
    while (i < lbp->size &&
           (idx = re_matchp(regex, p + i, &len)) != -1) {
      int w_index = 0;
      while (w_index < len) {
        if (!keywords[k].flag || !matchwordedge(p[idx + w_index])) {
          word_color[idx + w_index] = (int)keywords[k].color;
        }
        w_index++;
      }
      i += idx + len;
    }
  }
  for (int j = 0; j < lbp->size; j++) {
    printf("%s%c%s", colors[word_color[j]], p[j], COLOR_clear);
  }

  printf("\n");
}

// display
void display(struct linebuffer *head) {
  int i, v;
  struct linebuffer *lbp;

  v = statusbar.visibility == STATUSBAR_VISIBLE;
  i = v ? SCREEN_HEIGHT : SCREEN_HEIGHT - 1;
  lbp = head;

  if (is_change) {
    term_cursor_location(0, 0);
    fprintf(stdout, "\033[2J");
    while (i-- > 0) {
      if (is_hightlight) {
        fprintf(stdout, "%s\n", lbp->buf);
      } else {
        printline1(lbp);
      }
      lbp = lbp->next;
    }
  } else {
    int j = 1;
    while (j <= i) {
      term_cursor_location(0, j);
      if (is_change || lbp->dirty > 0) {
        if (lbp->dirty == 2) {
          is_change = 1;
        }

        printf("\033[2K");
        if (is_hightlight) {
          fprintf(stdout, "%s\n", lbp->buf);
        } else {
          printline1(lbp);
        }
      }
      lbp = lbp->next;
      j++;
    }
    // clear the status bar
    term_cursor_location(0, SCREEN_HEIGHT + 1);
    printf("\033[2K");
    term_cursor_location(0, SCREEN_HEIGHT + 1);
  }

  if (v) fprintf(stdout, "%s  %s", statusbar.mode, statusbar.msg);

  terminal_cursor_update();
  is_change = 0;
}

// status bar
void set_statusbar_mode(char *m) { strcpy(statusbar.mode, m); }

void statusbar_init() {
  set_statusbar_mode("-- NORMAL --");
  statusbar.visibility = STATUSBAR_VISIBLE;
  statusbar.msglength = 0;
}
void set_statusbar_message(char *m) {
  int i = 0;
  while (*(m + i) != '\0') i++;

  statusbar.msglength = i;
  strcpy(statusbar.msg, m);
}

void set_statusbar_visibility(int v) { statusbar.visibility = v; }

void statusbar_command_end();

void insert_statusbar_message(char c) {
  int i;
  for (i = 0; i + 1 < STATUSBAR_MESSAGE_LENGTH; i++) {
    if (c == KEYCODE_DELETE && statusbar.msg[i + 1] == '\0') {
      statusbar.msg[i] = '\0';
      statusbar.msglength--;
      if (i == 0) {
        statusbar_command_end();
      }
      return;
    }
    if (statusbar.msg[i] == '\0') {
      statusbar.msglength++;
      statusbar.msg[i] = c;
      statusbar.msg[i + 1] = '\0';
      return;
    }
  }
}

void clear_statusbar_message() {
  memset(statusbar.msg, 0, STATUSBAR_MESSAGE_LENGTH);
  statusbar.msglength = 0;
}

void statusbar_command_begin() {
  command = 1;
  set_statusbar_message(":");
}

void statusbar_command_end() {
  command = 0;
  clear_statusbar_message();
}

void statusbar_command_exec() {
  int i;
  char *arg1, *arg2;

  i = 0;
  while ((statusbar.msg[i] == ' ' || statusbar.msg[i] == ':') &&
         statusbar.msg[i] != '\0')
    i++;
  arg1 = statusbar.msg + i;

  if (statusbar.msg[i] != '\0') i++;

  while (statusbar.msg[i] == ' ' && statusbar.msg[i] != '\0') i++;
  arg2 = statusbar.msg + i;

  switch (*arg1) {
    case 'e':
      if (*arg2 != '\0') {
        strcpy(inputfilename, arg2);
        load();
      }
      break;
    case 'w':
      if (*arg2 != '\0') {
        strcpy(outputfilename, arg2);
      } else if (inputfilename[0] != '\0') {
        strcpy(outputfilename, inputfilename);
      } else {
        strcpy(outputfilename, "default.viout");
      }

      save();
      break;
    case 'q':
      quit();
      break;
    default:
      break;
  }
  clear_statusbar_message();
}

void error(char *msg) { set_statusbar_message(msg); }

// buffer
void link_linebuffer(struct linebuffer *l, struct linebuffer *r) {
  l->next = r;
  if (r != NULL) {
    r->prev = l;
  }
}

void alloc_linebuffer(struct linebuffer *lb) {
  lb->buf = malloc(LINE_BUFFER_LENGTH);
  lb->size = 0;
  lb->prev = 0;
  lb->next = 0;
  lb->dirty = 0;
}

struct linebuffer *create_linebuffer() {
  struct linebuffer *lbp;
  lbp = malloc(sizeof(struct linebuffer));
  alloc_linebuffer(lbp);
  return lbp;
}

// 如果lhs的size加上rhs的大小比linebuf大则放弃合并。将lhs与rhs合并，合并后rhs应该废弃不用
int merge_linebuffer(struct linebuffer *lhs, struct linebuffer *rhs) {
  // 成功返回1, 否则返回-1
  if (lhs->size + rhs->size >= LINE_BUFFER_LENGTH) {
    return -1;
  }
  for (int i = 0; i < rhs->size; i++) {
    lhs->buf[lhs->size + i] = rhs->buf[i];
  }
  lhs->size += rhs->size;
  return 1;
}

// mode
void mode_change(int m) {
  mode = m;
  switch (m) {
    case MODE_INSERT:
      set_statusbar_mode("-- INSERT --");
      return;
    case MODE_NORMAL:
      set_statusbar_mode("-- NORMAL --");
      return;
  }
}

void change_hightlight() {
  if (is_hightlight) {
    is_hightlight = 0;
    is_change = 1;
    error("syntax hightlight on");
    return;
  }
  is_hightlight = 1;
  is_change = 1;
  error("syntax hightlight off");
}

void delete_normal() {
  if (cursor.x == cursor.linebuffer->size) return;
  // is_change = 1;
  cursor.linebuffer->dirty = 1;

  memmove(cursor.linebuffer->buf + cursor.x,
          cursor.linebuffer->buf + cursor.x + 1,
          cursor.linebuffer->size - cursor.x);

  cursor.linebuffer->buf[cursor.linebuffer->size] = '\0';
  if (cursor.linebuffer->size > 0) cursor.linebuffer->size--;

  if (cursor.x > cursor.linebuffer->size) {
    cursor.x = cursor.linebuffer->size;
  }
}

void deleteline_normal() {
  struct linebuffer *p, *n;

  p = cursor.linebuffer->prev;
  n = cursor.linebuffer->next;

  // is_change = 1;

  if (p == &linebuffer_head && n == &linebuffer_tail) {
    
    // 保存删除的行以便等会复制时恢复
    if (last_delete_line != NULL) {
      free(last_delete_line->buf);
      free(last_delete_line);
    }
    last_delete_line = create_linebuffer();
    memcpy(last_delete_line->buf, cursor.linebuffer->buf, LINE_BUFFER_LENGTH);
    last_delete_line->size = cursor.linebuffer->size;

    memset(cursor.linebuffer->buf, '\0', LINE_BUFFER_LENGTH);
    cursor.linebuffer->size = 0;
    cursor.x = 0;
    cursor.y = 1;
    cursor.linebuffer->dirty = 2;
    return;
  }

  if (cursor.linebuffer == screen.upperline) {
    screen.upperline = cursor.linebuffer->next;
  }
  link_linebuffer(p, n);

  // 保存删除的行以便等会复制时恢复
  if (last_delete_line != NULL) {
    free(last_delete_line->buf);
    free(last_delete_line);
  }
  last_delete_line = cursor.linebuffer;

  cursor.linebuffer = n;
  cursor_up();
  if (cursor.x > cursor.linebuffer->size) {
    cursor.x = cursor.linebuffer->size;
  }
  cursor.linebuffer->dirty = 2;
}

// 不会读\n
char *fgets(int fd, char *buf, int max) {
  int i, cc;
  char c;

  for (i = 0; i + 1 < max;) {
    cc = read(fd, &c, 1);
    if (cc < 1) break;
    if (c == '\n') break;
    buf[i++] = c;
  }
  buf[i] = '\0';
  return buf;
}

void save() {
  struct linebuffer *lbp;
  int fd;

  fd = open(outputfilename, O_CREATE | O_TRUNC | O_WRONLY);

  if (fd < 0) {
    exit(-1);
  }

  lbp = linebuffer_head.next;
  while (lbp != &linebuffer_tail) {
    fprintf(fd, "%s\n", lbp->buf);
    lbp = lbp->next;
  }

  close(fd);
}

void load() {
  char buf[LINE_BUFFER_LENGTH];

  struct linebuffer *lbp, *lbpnext;

  int fd = open(inputfilename, O_RDONLY);
  if (fd < 0) {
    return;
  }

  lbp = &linebuffer_head;
  fgets(fd, buf, sizeof(buf));
  while (buf[0] != 0) {
    lbpnext = create_linebuffer();
    strcpy(lbpnext->buf, buf);
    lbpnext->size = strlen(buf);
    link_linebuffer(lbp, lbpnext);
    lbp = lbpnext;
    memset(buf, '\0', sizeof(buf));
    fgets(fd, buf, sizeof(buf));
  }
  link_linebuffer(lbp, &linebuffer_tail);

  cursor.linebuffer = linebuffer_head.next;
  cursor.x = 0;
  cursor.y = 1;
  screen.upperline = linebuffer_head.next;

  close(fd);
  is_change = 1;
}

void quit() { quit_flg = 1; }

int ischaracter(char c) { return 0x20 <= c && c <= 0x7e; }

void find_string() {
  if (find_str[0] == 0) {
    return;
  }

  struct linebuffer *lbp = cursor.linebuffer;
  int i = cursor.x;
  int j = 0;
  int found = 0;
  char *p = 0;
  int find_str_length = strlen(find_str);

  while (lbp != &linebuffer_tail) {
    p = cursor.linebuffer->buf;
    while (i < cursor.linebuffer->size) {
      if (p[i] == find_str[j]) {
        i++;
        j++;
        if (j == find_str_length) {
          found = 1;
          break;
        }
      } else if (j != 0) {
        j = 0;
      } else {
        i++;
      }
    }
    if (found) break;
    cursor_down();
    lbp = lbp->next;
    i = 0;
    j = 0;
  }

  if (found) {
    cursor.x = i - find_str_length;
  } else {
    error("\033[31mHit bottom. Can't find string\e[0m");
  }
}

void reverse_find_string() {
  if (find_str[0] == 0) {
    return;
  }
  if (cursor.x == 0) {
    cursor_up();
  }
  int find_str_length = strlen(find_str);
  struct linebuffer *lbp = cursor.linebuffer;
  int i = cursor.x;
  int j = find_str_length - 1;
  int found = 0;
  char *p = 0;

  if (cursor.x == cursor.linebuffer->size) {
    i--;
  }

  while (lbp != &linebuffer_head) {
    p = cursor.linebuffer->buf;
    while (i >= 0) {
      if (p[i] == find_str[j]) {
        i--;
        j--;
        if (j == -1) {
          found = 1;
          break;
        }
      } else if (j != find_str_length - 1) {
        j = find_str_length - 1;
      } else {
        i--;
      }
    }
    if (found) break;
    cursor_up();
    lbp = lbp->prev;
    i = cursor.linebuffer->size - 1;
    j = find_str_length - 1;
  }

  if (found) {
    cursor.x = i + 1;
  } else {
    error("\033[31mHit top. Can't find string\e[0m");
  }
}

void input_command(char c) {
  switch (c) {
    case KEYCODE_ESC:
      statusbar_command_end();
      return;
    case KEYCODE_CR:
    case KEYCODE_LF:
      statusbar_command_exec();
      statusbar_command_begin();
      return;
    default:
      if (!ischaracter(c) && c != KEYCODE_DELETE) {
        return;
      }
      insert_statusbar_message(c);
      return;
  }
}

void handle_find() {
  int i = 0;
  char c;

  // 清空输入框
  term_cursor_location(STATUSBAR_MESSAGE_START, SCREEN_HEIGHT + 1);
  for (int i = 0; i < 30; i++) {
    printf(" ");
  }

  term_cursor_location(STATUSBAR_MESSAGE_START, SCREEN_HEIGHT + 1);
  printf("/");
  memset(find_str, 0, sizeof(find_str));
  while (read(stdin, &c, 1) > 0 && i < FIND_STR_LENGTH) {
    switch (c) {
      case KEYCODE_ESC:
        memset(find_str, 0, sizeof(find_str));
        return;
      case '\n':
        find_string();
        return;
      case KEYCODE_DELETE:
        printf("\b \b");
        if (i == 0) {
          return;
        }
        find_str[--i] = 0;
        break;
      default:
        printf("%c", c);
        find_str[i++] = c;
        break;
    }
  }
}

void handle_reverse_find() {
  int i = 0;
  char c;

  // 清空输入框
  term_cursor_location(STATUSBAR_MESSAGE_START, SCREEN_HEIGHT + 1);
  for (int i = 0; i < 30; i++) {
    printf(" ");
  }

  term_cursor_location(STATUSBAR_MESSAGE_START, SCREEN_HEIGHT + 1);
  printf("?");

  memset(find_str, 0, sizeof(find_str));
  while (read(stdin, &c, 1) > 0 && i < FIND_STR_LENGTH) {
    switch (c) {
      case KEYCODE_ESC:
        memset(find_str, 0, sizeof(find_str));
        return;
      case '\n':
        reverse_find_string();
        return;
      case KEYCODE_DELETE:
        printf("\b \b");
        if (i == 0) {
          return;
        }
        find_str[--i] = 0;
        break;
      default:
        printf("%c", c);
        find_str[i++] = c;
        break;
    }
  }
}

// 将行挂到当前行的下一行，同时光标下移
void paste_line() {
  if (last_delete_line == NULL) {
    return;
  }
  last_delete_line->dirty = 2;
  struct linebuffer *next_line = cursor.linebuffer->next;
  link_linebuffer(cursor.linebuffer, last_delete_line);
  link_linebuffer(last_delete_line, next_line);
  cursor_down();
  cursor.x = 0;

  struct linebuffer *new_line = create_linebuffer();
  new_line->size = last_delete_line->size;
  memcpy(new_line->buf, last_delete_line->buf, LINE_BUFFER_LENGTH);
  last_delete_line = new_line;
}

void input_mode_normal(char c) {
  if (command) {
    input_command(c);
    return;
  }

  clear_statusbar_message();

  switch (c) {
    case 'a':
      cursor_right();
      mode_change(MODE_INSERT);
      return;
    case 'i':
      mode_change(MODE_INSERT);
      return;
    case 'h':
      cursor_left();
      return;
    case 'j':
      cursor_down();
      return;
    case 'k':
      cursor_up();
      return;
    case 'l':
      cursor_right();
      return;
    case 'x':
      delete_normal();
      return;
    case 'd':
      deleteline_normal();
      return;
    case ':':
      statusbar_command_begin();
      return;
    case '/':
      handle_find();
      return;
    case '?':
      handle_reverse_find();
      return;
    case 'n':
      cursor_right();
      find_string();
      return;
    case 'N':
      cursor_left();
      reverse_find_string();
      return;
    case KEYCODE_CTRL_L:
      change_hightlight();
      return;
    case 'p':
      paste_line();
      return;
  }
}

void enter_insert() {
  struct linebuffer *lbp, *lbpnext;
  lbp = create_linebuffer();

  lbp->size = cursor.linebuffer->size - cursor.x;
  safestrcpy(lbp->buf, cursor.linebuffer->buf + cursor.x, lbp->size + 1);
  cursor.linebuffer->size = cursor.x;
  memset(cursor.linebuffer->buf + cursor.x, '\0',
         LINE_BUFFER_LENGTH - cursor.x);

  lbpnext = cursor.linebuffer->next;
  link_linebuffer(cursor.linebuffer, lbp);
  link_linebuffer(lbp, lbpnext);

  cursor.linebuffer->dirty = 2;
  cursor_down();
  cursor.x = 0;
}

void character_insert(char c) {
  if (cursor.linebuffer->size == LINE_BUFFER_LENGTH - 1) {
    return;
  }
  memmove(cursor.linebuffer->buf + (cursor.x + 1),
          cursor.linebuffer->buf + cursor.x,
          cursor.linebuffer->size - cursor.x + 1);
  cursor.linebuffer->buf[cursor.x] = c;

  cursor.linebuffer->dirty = 1;
  cursor.linebuffer->size++;
  cursor_right();
}

void handle_backspace() {
  if (cursor.x == 0 && cursor.y == 1) {
    return;
  }
  if (cursor.x == 0) {
    if (merge_linebuffer(cursor.linebuffer->prev, cursor.linebuffer) > 0) {
      int right_size = cursor.linebuffer->size;
      deleteline_normal();
      cursor.x = cursor.linebuffer->size - right_size;
    }
    return;
  }

  if (cursor.x == cursor.linebuffer->size) {
    cursor_left();
    delete_normal();
    cursor_right();
  } else {
    cursor_left();
    delete_normal();
  }
}

void handle_tab() {
  if (cursor.linebuffer->size + 4 >= LINE_BUFFER_LENGTH) {
    return;
  }
  for (int i = 0; i < 4; i++) {
    character_insert(' ');
  }
}

void input_mode_insert(char c) {
  clear_statusbar_message();

  switch (c) {
    case KEYCODE_ESC:
      mode_change(MODE_NORMAL);
      return;
    case KEYCODE_CR:
    case KEYCODE_LF:
      enter_insert();
      break;
    case KEYCODE_DELETE:
      handle_backspace();
      break;
    case KEYCODE_TAB:
      handle_tab();
      break;
    case KEYCODE_CTRL_L:
      change_hightlight();
      break;
    default:
      if (!ischaracter(c)) return;
      character_insert(c);
      break;
  }
  // is_change = 1;
}

void input_hook() {
  char c;
  read(stdin, &c, 1);

  switch (mode) {
    case MODE_NORMAL:
      set_statusbar_mode("-- NORMAL --");
      input_mode_normal(c);
      return;
    case MODE_INSERT:
      set_statusbar_mode("-- INSERT --");
      input_mode_insert(c);
      return;
    default:
      error("undefined mode");
      return;
  }
}

// init
void init() {
  struct linebuffer *lbp;
  lbp = create_linebuffer();

  mode = MODE_NORMAL;
  quit_flg = 0;
  cursor_init(lbp);
  statusbar_init();

  memset(find_str, 0, sizeof(find_str));

  alloc_linebuffer(&linebuffer_head);
  alloc_linebuffer(&linebuffer_tail);
  linebuffer_head.prev = &linebuffer_head;
  link_linebuffer(&linebuffer_tail, &linebuffer_tail);
  link_linebuffer(&linebuffer_head, lbp);
  link_linebuffer(lbp, &linebuffer_tail);
  strcpy(linebuffer_tail.buf, "~");
  linebuffer_tail.size = 1;

  // screen_init: after buffer initialization
  screen_init();
}

void cleanup() {
  struct linebuffer *lbp, *lbptmp;
  lbp = (&linebuffer_tail)->prev;
  while (lbp != &linebuffer_head) {
    lbptmp = lbp;
    lbp = lbp->prev;
    free(lbptmp->buf);
    free(lbptmp);
  }
  free(linebuffer_head.buf);
  free(linebuffer_tail.buf);
  if (last_delete_line != NULL) {
    free(last_delete_line->buf);
    free(last_delete_line);
  }
}

// main
int main(int argc, char *argv[]) {
  // struct linebuffer *top;

  init();
  setviflag();

  if (argc == 2) {
    strcpy(inputfilename, argv[1]);
    load();
  }

  while (1) {
    // top = screen_top();
    // display(top);
    display(screen.upperline);
    input_hook();

    if (quit_flg) break;
  }
  printf("\n");
  term_cursor_location(0, 0);
  fprintf(stdout, "\033[2J");

  eraseviflag();
  cleanup();

  exit(0);
}