/** includes **/
#define _DEFAULT_SOURCE //a few feature test macros
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <errno.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <termios.h>

/** defines **/
#define KILO_VERSION "0.0.1"
#define KILO_TAB_STOP 8

#define CTRL_KEY(k) (k & 0x1f) // bitwise AND operation floors the 5th and 6th bit to zero.

enum {
	ARROW_LEFT = 1000, //dealing with keys as integer values so user cant type out escape commands
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	DEL_KEY,
	HOME_KEY, // key combos: <esc>[1~, <esc>[7~, , <esc>[OH, <esc>[H
	END_KEY, // key combos: <esc>[4~, <esc>[8~, <esc>[F
	PAGE_UP,
	PAGE_DOWN,
};

/** data **/

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
	struct termios orig_termios; //original terminal settings.
};

struct editorConfig E;

/** terminal **/
void die(const char *c) {
	//clear display on quit
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);

	perror(c);
	exit(1);
}

void disableRawMode(){
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH,  &E.orig_termios) == -1) die("tcsetattr");
}

void enableRawMode(){
	if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1 ) die("tcsetattr"); //read current terminal attributes into struct
	atexit(disableRawMode);

	struct termios raw = E.orig_termios;


	//note: some of these flags aren't really used and won't have a tangible effect, but whoever designed "raw mode" decided
	//they should be turned off. I labeled the important ones.

	//i_flag: input flags
	//IXON: flow control (ctrl-s, ctrl-q)
	//ICRNL: carriage returns (ctrl-m)
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

	//oflag: output flag
	//OPOST: automatically prepends each \n with a carriage return
	raw.c_oflag &= ~(OPOST);

	raw.c_cflag |= (CS8);

	//c_lflag: misc flags; contains both echo and icanon
	//ECHO: prints typed keys to terminal
	//ICANON: Canonical mode. Waits before ENTER is hit before taking input
	//ISIG: contains SIGINT and SIGSTP, or the program killing commands (ctrl c, ctrl z)
	//IEXTEN: waits for input then sends that as literal char (ctrl-v)
	raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN); 	

	raw.c_cc[VMIN] = 0; //minimum bytes of input before read() returns
	raw.c_cc[VTIME] = 1; //if 100 milliseconds pass, read() returns

	//load in struct. TCSAFLUSH will wait for output to be written to term before setting.
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr"); 
}


int editorReadKey(){
	char c;
	int nread;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN) die("read");
	}

	if (c == '\x1b') {
		char seq[3];

		//if read fails (it times out) c is 1 char long and must just be the escape character
		if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
		if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

		if (seq[0] == '[') {
			if (seq[1] >= '0' && seq[1] <= '9') { //if sequence is digit
				if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
				if (seq[2] == '~') {
					switch (seq[1]) {
						case('1'):
							return HOME_KEY;
						case('3'):
							return DEL_KEY;
						case('4'):
							return END_KEY;
						case('5'):
							return PAGE_UP;
						case('6'):
							return PAGE_DOWN;
						case('7'):
							return HOME_KEY;
						case('8'):
							return END_KEY;
					}
				}
			} else {
				switch (seq[1]) { //convert to wasd
					case 'A': return ARROW_UP;
      					case 'B': return ARROW_DOWN;
      					case 'C': return ARROW_RIGHT;
      					case 'D': return ARROW_LEFT;
					case 'H': return HOME_KEY;
					case 'F': return END_KEY;
      				}	
			}
			return '\x1b';
		} else if (seq[1] == 'O') {
			switch (seq[2]) {
				case 'H': return HOME_KEY;
				case 'F': return END_KEY;
			}
		}
	}
	return c;
	
}

int getWindowSize(int *rows, int *cols){
	struct winsize ws;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)  == -1 || ws.ws_col == 0) { //TIOCGWINSZ grabs terminal window size
		return -1;
	}
	else {
		*rows = ws.ws_row;
		*cols= ws.ws_col;
		return 0;
	}
}

/** append buffer **/
struct abuf {
	char* b;
	int len;
};
#define ABUF_INIT {NULL, 0} //append buffer initialization

void abAppend(struct abuf *ab, const char *s, int len){
	char* new = realloc(ab->b, ab->len + len); //give our new string whatever was in the old buffer plus new length
	
	if (new == NULL) return;
	memcpy(&new[ab -> len], s, len); //copy the new string into the end of new
	ab->b = new;
	ab->len += len;
}

void abFree(struct abuf *ab){
	free(ab -> b);
}

/** row operations **/
int editorRowCxToRx(erow *row, int cx) {
	int rx = 0;
	int j;
	for (j = 0; j < cx; j++) {
		if (row->chars[j] == '\t') rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
		rx++;
	}
	return rx;
}

void editorUpdateRow(erow *row) {
	int tabs = 0;
	int j;
	for (j = 0; j < row->size; j++){
		if (row->chars[j] == '\t') tabs++;
	}
	free(row->render);
	row->render = malloc(row->size + tabs * (KILO_TAB_STOP - 1) + 1); //mallocing enough tab room for 8 spaces: row->size alr contains 1 space

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

void editorAppendRow(char* s, size_t len){
	E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

	int at = E.numrows;
	E.row[at].size = len;
	E.row[at].chars = malloc(len + 1);
	memcpy(E.row[at].chars, s, len);
	E.row[at].chars[len] = '\0';
	E.numrows++;

	E.row[at].rsize = 0;
	E.row[at].render = NULL;

	editorUpdateRow(&E.row[at]);
}

/** file i/o **/
void editorOpen(char *filename){
	FILE *fp = fopen(filename, "r");
	if (!fp) die("fopen");

	char* line = NULL;
	size_t linecap = 0;
	ssize_t linelen;

	while ((linelen = getline(&line, &linecap, fp)) != -1) { //getline will throw -1 if end of file is reached
		if (linelen != 1) {
			while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) linelen--;//cut out new line char
			editorAppendRow(line,linelen);
		}
	}

	free(line);
}

/** output **/
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


//Note: write commands are from VT100 where \x1b is the escape character
void editorDrawRows(struct abuf *ab){
	editorScroll();

	for (int y = 0; y < E.screenrows; y++) {
		int filerow = y + E.rowoff;
		if (filerow >= E.numrows) {
			if (E.numrows == 0 && y == E.screenrows / 3) {
				char welcome[80];
				//copy welcome message into welcome buffer
				int welcomeln = snprintf(welcome, sizeof(welcome), "Kilo editor -- version %s", KILO_VERSION);
				if (welcomeln == E.screenrows) welcomeln = E.screenrows;

				int padding = (E.screencols - welcomeln) / 2;
				if (padding) { //draw tilde at beginning of line
					abAppend(ab, "~", 1);
					padding--;
				}
				while(padding--) abAppend(ab, " ", 1);
				abAppend(ab, welcome, welcomeln);

			} else {
				abAppend(ab, "~", 1);
			}
		} else {
			int len = E.row[filerow].rsize - E.coloff;
			if (len < 0) len = 0;
			if (len > E.screencols) len = E.screencols;
			abAppend(ab, &E.row[filerow].render[E.coloff], len);
		}

		abAppend(ab, "\x1b[K", 3); //Erase In Display: clear line to left of cursor
		abAppend(ab, "\r\n", 2);
	}
}

void editorRefreshScreen() {
	struct abuf ab = ABUF_INIT;
	

	abAppend(&ab, "\x1b[?25l", 6); //Erase Cursor
	abAppend(&ab, "\x1b[H", 4); //Cursor Position: positions cursor to 0,0
	editorDrawRows(&ab);

	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff)+ 1);
	abAppend(&ab, buf, strlen(buf));

	abAppend(&ab, "\x1b[?25h", 6); //Reenable Cursor

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

/** input **/
void editorMoveCursor(int key){
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
			} else if (E.cy < E.numrows) {
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
	int rowlen = (row) ? row-> size : 0;
	if (E.cx > rowlen) E.cx = rowlen;
}

void editorProcessKeypress() {
	int c = editorReadKey();

	switch (c) {
		case CTRL_KEY('q'):
			//clear display on quit
			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);

			exit(0);
			break;
		
		case HOME_KEY:
			E.cx = 0;
			break;
		case END_KEY:
			if (E.cy < E.numrows) {
				E.cx = E.row[E.cy].size;
			}
			break;

		case PAGE_DOWN:
		case PAGE_UP:
			{
				if(c == PAGE_UP) E.cy = E.rowoff;
				else if(c == PAGE_DOWN) E.cy = E.rowoff + E.screenrows - 1;
				if(E.cy > E.numrows) E.cy = E.numrows;
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
	}
}

/** init **/
void initEditor() {
	E.cx = 0;
	E.cy = 0;
	E.rx = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.numrows = 0;
	E.row = NULL;

	if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
	E.screenrows -= 1; //room for status bar
}

int main (int argc, char *argv[]){
	enableRawMode();
	initEditor();

	if (argc >= 2) {
		editorOpen(argv[1]);
	}

	while(1){
		editorRefreshScreen();
		editorProcessKeypress();
	}
	return 0;
}
