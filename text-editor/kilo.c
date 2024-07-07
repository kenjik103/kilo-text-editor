/** includes **/

#include <errno.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <termios.h>

/** defines **/
#define CTRL_KEY(k) (k & 0x1f) // bitwise AND operation floors the 5th and 6th bit to zero.
#define KILO_VERSION "0.0.1"

/** data **/
struct editorConfig {
	int cx, cy;
	int screenrows;
	int screencols;
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


char editorReadKey(){
	char c;
	int nread;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN) die("read");
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


/** output **/
//Note: write commands are from VT100 where \x1b is the escape character

void editorDrawRows(struct abuf *ab){
	for (int y = 0; y < E.screenrows; y++) {
		if (y == E.screenrows / 3) {
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

		abAppend(ab, "\x1b[K", 3); //Erase In Display: clear line to left of cursor

		if (y < E.screenrows - 1) {
			abAppend(ab, "\r\n", 2);
		}
	}
}

void editorRefreshScreen() {
	struct abuf ab = ABUF_INIT;
	

	abAppend(&ab, "\x1b[?25l", 6); //Erase Cursor
	abAppend(&ab, "\x1b[H", 4); //Cursor Position: positions cursor to 0,0
	editorDrawRows(&ab);

	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
	abAppend(&ab, buf, strlen(buf));

	abAppend(&ab, "\x1b[?25h", 6); //Reenable Cursor

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

/** input **/
void editorMoveCursor(char key){
	switch (key) {
		case 'a':
			E.cx--;
			break;
		case 'd':
			E.cx++;
			break;
		case 'w':
			E.cy--;
			break;
		case 's':
			E.cy++;
			break;
	}
}

void editorProcessKeypress() {
	char c = editorReadKey();

	switch (c) {
		case CTRL_KEY('q'):
			//clear display on quit
			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);

			exit(0);
			break;

		case 'w':
		case 'a':
		case 's':
		case 'd':
			editorMoveCursor(c);
			break;
	}
}

/** init **/
void initEditor() {
	E.cx = 0;
	E.cy = 0;

	if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main (){
	enableRawMode();
	initEditor();

	while(1){
		editorRefreshScreen();
		editorProcessKeypress();
	}
	return 0;
}
