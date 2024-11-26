#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <sys/wait.h>
#include <ncurses.h>

#define GRID_SIZE 8
#define EMPTY '.'
#define HIT 'X'
#define MISS 'O'
#define SHIP 'S'
#define SHIP_GAP 1
#define WINDOW_HEIGHT 24
#define WINDOW_WIDTH 80

const float DELAY_BETWEEN_TURNS = 0.5; 

void custom_delay(float seconds) {
    struct timespec ts;
    ts.tv_sec = (time_t)seconds;
    ts.tv_nsec = (long)((seconds - (float)ts.tv_sec) * 1000000000L);
    nanosleep(&ts, NULL);
}

typedef struct {
    int size;
    int count;
} Ship;

Ship ships[] = {
    {4, 1}, /* 1 Battleship (4 cells) */
    {3, 2}, /* 2 Cruisers (3 cells) */
    {2, 2}  /* 2 Destroyers (2 cells) */
};

char grid1[GRID_SIZE][GRID_SIZE]; /* Child's grid */
char grid2[GRID_SIZE][GRID_SIZE]; /* Parent's grid */

typedef struct {
    int row;
    int col;
} Coordinate;

Coordinate hitQueue[GRID_SIZE * GRID_SIZE];
int hitQueueSize = 0;
int currentPlayer = 0;
int shotsMade[GRID_SIZE][GRID_SIZE] = {0};

void resetShotsMade() {
    for (int i = 0; i < GRID_SIZE; i++) {
        for (int j = 0; j < GRID_SIZE; j++) {
            shotsMade[i][j] = 0;
        }
    }
}

WINDOW *create_centered_win(int height, int width) {
    int starty, startx;
    int term_height, term_width;
    getmaxyx(stdscr, term_height, term_width);

    starty = (term_height - height) / 2;
    startx = (term_width - width) / 2;

    WINDOW *local_win = newwin(height, width, starty, startx);
    box(local_win, 0, 0);
    wrefresh(local_win);
    return local_win;
}
WINDOW *create_newwin(int height, int width, int starty, int startx) {
    WINDOW *local_win;

    local_win = newwin(height, width, starty, startx);
    box(local_win, 0 , 0);
    wrefresh(local_win);
    
    return local_win;
}

void init_colors() {
    start_color();
    init_pair(1, COLOR_BLUE, COLOR_BLACK);    // Water/Empty
    init_pair(2, COLOR_GREEN, COLOR_BLACK);   // Ship
    init_pair(3, COLOR_RED, COLOR_BLACK);     // Hit
    init_pair(4, COLOR_WHITE, COLOR_BLACK);   // Miss
    init_pair(5, COLOR_YELLOW, COLOR_BLACK);  // Headers
}

void initialize_ncurses() {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    if (has_colors()) {
        init_colors();
    }
    curs_set(0);
}

void cleanup_ncurses() {
    endwin();
}

void saveGame(const char *filename) {
    FILE *file = fopen(filename, "wb");
    if (!file) {
        perror("Failed to open file for saving");
        return;
    }

    fwrite(grid1, sizeof(grid1), 1, file);
    fwrite(grid2, sizeof(grid2), 1, file);
    fwrite(&hitQueueSize, sizeof(hitQueueSize), 1, file);
    fwrite(hitQueue, sizeof(Coordinate), hitQueueSize, file);
	
    fclose(file);
}

int loadGame(const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Failed to open file for loading");
        return 0;
    }

    fread(grid1, sizeof(grid1), 1, file);
    fread(grid2, sizeof(grid2), 1, file);
    fread(&hitQueueSize, sizeof(hitQueueSize), 1, file);
    fread(hitQueue, sizeof(Coordinate), hitQueueSize, file);

    resetShotsMade();

    fclose(file);
    printw("Game loaded successfully.\n");
    refresh();

    currentPlayer = (hitQueueSize % 2);

    refresh();
    return 1;
}

void initializeGrid(char grid[GRID_SIZE][GRID_SIZE]) {
    for (int i = 0; i < GRID_SIZE; i++) {
        for (int j = 0; j < GRID_SIZE; j++) {
            grid[i][j] = EMPTY;
        }
    }
}

void printGridNcurses(WINDOW *win, char grid[GRID_SIZE][GRID_SIZE], int starty, int startx, int show_ships) {
    wmove(win, starty, startx + 3);
    wattron(win, COLOR_PAIR(5));
    for (int j = 0; j < GRID_SIZE; j++) {
        mvwprintw(win, starty, startx + 3 + j * 2, "%d", j);
    }
    wattroff(win, COLOR_PAIR(5));

    for (int i = 0; i < GRID_SIZE; i++) {
        wattron(win, COLOR_PAIR(5));
        mvwprintw(win, starty + 1 + i, startx, "%d", i);
        wattroff(win, COLOR_PAIR(5));
        
        for (int j = 0; j < GRID_SIZE; j++) {
            char display = grid[i][j];
            if (!show_ships && display == SHIP) {
                display = EMPTY;
            }
            wmove(win, starty + 1 + i, startx + 3 + j * 2);
            switch(display) {
                case EMPTY:
                    wattron(win, COLOR_PAIR(1));
                    waddch(win, EMPTY);
                    wattroff(win, COLOR_PAIR(1));
                    break;
                case SHIP:
                    wattron(win, COLOR_PAIR(2));
                    waddch(win, SHIP);
                    wattroff(win, COLOR_PAIR(2));
                    break;
                case HIT:
                    wattron(win, COLOR_PAIR(3));
                    waddch(win, HIT);
                    wattroff(win, COLOR_PAIR(3));
                    break;
                case MISS:
                    wattron(win, COLOR_PAIR(4));
                    waddch(win, MISS);
                    wattroff(win, COLOR_PAIR(4));
                    break;
            }
        }
    }
    wrefresh(win);
}

int canPlaceShip(char grid[GRID_SIZE][GRID_SIZE], int row, int col, int size, int orientation) {
    int i;

    if (orientation == 0 && col + size > GRID_SIZE) return 0;
    if (orientation == 1 && row + size > GRID_SIZE) return 0;

    for (i = -1; i <= size; i++) {
        for (int j = -1; j <= 1; j++) {
            int r = row + (orientation == 1 ? i : j);
            int c = col + (orientation == 0 ? i : j);
            
            if (r >= 0 && r < GRID_SIZE && c >= 0 && c < GRID_SIZE) {
                if (i >= 0 && i < size) {
                    if (j == 0 && grid[orientation == 1 ? r : row][orientation == 0 ? c : col] != EMPTY) {
                        return 0;
                    }
                }
                if (grid[r][c] == SHIP) return 0;
            }
        }
    }
    return 1;
}

void placeShip(char grid[GRID_SIZE][GRID_SIZE], int size) {
    int row, col, orientation;
    int maxAttempts = 100;
    int attempts = 0;
    
    do {
        orientation = rand() % 2;
        row = rand() % GRID_SIZE;
        col = rand() % GRID_SIZE;
        attempts++;
        if (attempts >= maxAttempts) {
            printf("Warning: Could not place ship of size %d\n", size);
            return;
        }
    } while (!canPlaceShip(grid, row, col, size, orientation));

    for (int i = 0; i < size; i++) {
        int r = row + (orientation == 1 ? i : 0);
        int c = col + (orientation == 0 ? i : 0);
        grid[r][c] = SHIP;
    }
}

void placeShips(char grid[GRID_SIZE][GRID_SIZE]) {
    for (int i = 0; i < sizeof(ships) / sizeof(ships[0]); i++) {
        for (int j = 0; j < ships[i].count; j++) {
            placeShip(grid, ships[i].size);
        }
    }
}

int checkHit(char grid[GRID_SIZE][GRID_SIZE], int row, int col) {
    if (grid[row][col] == SHIP) {
        grid[row][col] = HIT;
        return 1;
    }
    if (grid[row][col] == EMPTY) {
        grid[row][col] = MISS;
    }
    return 0;
}

int allShipsSunk(char grid[GRID_SIZE][GRID_SIZE]) {
    for (int i = 0; i < GRID_SIZE; i++) {
        for (int j = 0; j < GRID_SIZE; j++) {
            if (grid[i][j] == SHIP) return 0;
        }
    }
    return 1;
}

void enqueueHit(int row, int col) {
    if (hitQueueSize < GRID_SIZE * GRID_SIZE) {
        hitQueue[hitQueueSize].row = row;
        hitQueue[hitQueueSize].col = col;
        hitQueueSize++;
    }
}

Coordinate dequeueHit() {
    Coordinate coord = hitQueue[0];
    for (int i = 1; i < hitQueueSize; i++) {
        hitQueue[i - 1] = hitQueue[i];
    }
    hitQueueSize--;
    return coord;
}

int hasPendingHits() {
    return hitQueueSize > 0;
}

void turnAI(WINDOW *win, char opponentGrid[GRID_SIZE][GRID_SIZE], const char* playerName) {
    int row, col;
    int attempts = 0;
    const int MAX_ATTEMPTS = 100;  // Add maximum attempts to prevent infinite loops

    if (hasPendingHits()) {
        Coordinate coord = dequeueHit();
        row = coord.row;
        col = coord.col;
    } else {
        do {
            row = rand() % GRID_SIZE;
            col = rand() % GRID_SIZE;
            attempts++;
            if (attempts >= MAX_ATTEMPTS) {
                // If we can't find a valid move after MAX_ATTEMPTS, reset the shotsMade array
                resetShotsMade();
                row = rand() % GRID_SIZE;
                col = rand() % GRID_SIZE;
                break;
            }
        } while (opponentGrid[row][col] == HIT || opponentGrid[row][col] == MISS || shotsMade[row][col] == 1);
    }

    shotsMade[row][col] = 1;
    if (checkHit(opponentGrid, row, col)) {
        mvwprintw(win, WINDOW_HEIGHT - 3, 2, "%s hits at (%d, %d)!          ", playerName, row, col);
        // Add validity checks for enqueueHit
        if (row > 0 && opponentGrid[row - 1][col] != HIT && opponentGrid[row - 1][col] != MISS && !shotsMade[row - 1][col]) 
            enqueueHit(row - 1, col);
        if (row < GRID_SIZE - 1 && opponentGrid[row + 1][col] != HIT && opponentGrid[row + 1][col] != MISS && !shotsMade[row + 1][col]) 
            enqueueHit(row + 1, col);
        if (col > 0 && opponentGrid[row][col - 1] != HIT && opponentGrid[row][col - 1] != MISS && !shotsMade[row][col - 1]) 
            enqueueHit(row, col - 1);
        if (col < GRID_SIZE - 1 && opponentGrid[row][col + 1] != HIT && opponentGrid[row][col + 1] != MISS && !shotsMade[row][col + 1]) 
            enqueueHit(row, col + 1);
    } else {
        mvwprintw(win, WINDOW_HEIGHT - 3, 2, "%s misses at (%d, %d).         ", playerName, row, col);
    }
    wrefresh(win);
    saveGame("saved_game.dat");
}

void startNewGame() {
    WINDOW *main_win = create_centered_win(WINDOW_HEIGHT, WINDOW_WIDTH);
   
    resetShotsMade();
    
    int pipefds[2];
    if (pipe(pipefds) == -1) {
        mvwprintw(main_win, WINDOW_HEIGHT - 1, 2, "Pipe failed");
        wrefresh(main_win);
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        mvwprintw(main_win, WINDOW_HEIGHT - 1, 2, "Fork failed");
        wrefresh(main_win);
        return;
    }

    if (pid == 0) { /* Child process */
        close(pipefds[0]);
        srand(time(NULL) ^ getpid());
        initializeGrid(grid1);
        placeShips(grid1);
        write(pipefds[1], grid1, sizeof(grid1));
        close(pipefds[1]);
        exit(0);
    }

    /* Parent process */
    close(pipefds[1]);
    srand(time(NULL) ^ getpid());
    initializeGrid(grid2);
    placeShips(grid2);
    read(pipefds[0], grid1, sizeof(grid1));
    close(pipefds[0]);
    
    int status;
    waitpid(pid, &status, 0);
    hitQueueSize = 0;

    mvwprintw(main_win, 1, 2, "Parent's Grid");
    mvwprintw(main_win, 1, WINDOW_WIDTH / 2 + 2, "Child's Grid");
    
    while (1) {
        mvwprintw(main_win, WINDOW_HEIGHT-3, 2, "                                        ");
        mvwprintw(main_win, WINDOW_HEIGHT-2, 2, "                                        ");
        
        printGridNcurses(main_win, grid2, 2, 2, 1);
        printGridNcurses(main_win, grid1, 2, WINDOW_WIDTH/2 + 2, 1);

        mvwprintw(main_win, WINDOW_HEIGHT-4, 2, "Parent's turn:");
        wrefresh(main_win);
        custom_delay(DELAY_BETWEEN_TURNS);
        
        mvwprintw(main_win, WINDOW_HEIGHT-4, 2, "Parent's turn: Thinking...");
        wrefresh(main_win);
        custom_delay(DELAY_BETWEEN_TURNS);
        
        turnAI(main_win, grid1, "Parent");
        if (allShipsSunk(grid1)) {
            mvwprintw(main_win, WINDOW_HEIGHT-2, 2, "Parent process wins!");
            wrefresh(main_win);
            custom_delay(DELAY_BETWEEN_TURNS * 3);
            break;
        }

        custom_delay(DELAY_BETWEEN_TURNS);
        
        mvwprintw(main_win, WINDOW_HEIGHT-4, 2, "Child's turn:");
        wrefresh(main_win);
        custom_delay(DELAY_BETWEEN_TURNS);
        
        mvwprintw(main_win, WINDOW_HEIGHT-4, 2, "Child's turn: Thinking...");
        wrefresh(main_win);
        custom_delay(DELAY_BETWEEN_TURNS);
        
        turnAI(main_win, grid2, "Child");
        if (allShipsSunk(grid2)) {
            mvwprintw(main_win, WINDOW_HEIGHT-2, 2, "Child process wins!");
            wrefresh(main_win);
            custom_delay(DELAY_BETWEEN_TURNS * 3);
            break;
        }
        
        custom_delay(DELAY_BETWEEN_TURNS);
    }
    
    delwin(main_win);
}

void showMenu(WINDOW *menu_win) {
    int height, width;
    getmaxyx(menu_win, height, width);  
    int startX = width / 4 ;  
    int startY = height / 4; 

    wclear(menu_win);
    box(menu_win, 0, 0);

    mvwprintw(menu_win, startY-1, startX-3, "--- Battleship game ---");

    mvwprintw(menu_win, startY + 1, startX-3, "1. Start New Game");
    mvwprintw(menu_win, startY + 2, startX-3, "2. View Boards");
    mvwprintw(menu_win, startY + 3, startX-3, "3. Redeploy Ships");
    mvwprintw(menu_win, startY + 4, startX-3, "4. Load Game");
    mvwprintw(menu_win, startY + 5, startX-3, "5. Quit Game");

    mvwprintw(menu_win, startY + 7, startX-3, "Make your choice(1-5): ");
    wrefresh(menu_win);
}

int main() {
    initialize_ncurses();
    
    WINDOW *menu_win = create_newwin(12, 30, (LINES - 12)/2, (COLS - 30)/2);
    WINDOW *main_win = NULL;
    int choice;
    char input[10];

    while (1) {
        showMenu(menu_win);
        echo();
        wgetstr(menu_win, input);
        noecho();
        choice = atoi(input);
        
        mvwprintw(menu_win, 8, 24, "%s", input);
        wrefresh(menu_win);
        custom_delay(DELAY_BETWEEN_TURNS);

        switch (choice) {
            case 1:
            delwin(menu_win);
            startNewGame();
            menu_win = create_newwin(12, 30, (LINES - 12)/2, (COLS - 30)/2);
            break;
            case 2:
                delwin(menu_win);
                {
                    WINDOW *view_win = create_newwin(WINDOW_HEIGHT, WINDOW_WIDTH, 0, 0);
                    mvwprintw(view_win, 1, 2, "Valid game situation");
                    mvwprintw(view_win, 1, 2, "Parent's Grid");
                    mvwprintw(view_win, 1, WINDOW_WIDTH/2 + 2, "Child's Grid");
                    printGridNcurses(view_win, grid1, 3, 2, 1);
                    printGridNcurses(view_win, grid2, 3, WINDOW_WIDTH/2 + 2, 1);
                    mvwprintw(view_win, WINDOW_HEIGHT-2, 2, "Press any key to continue...");
                    wrefresh(view_win);
                    wgetch(view_win);
                    delwin(view_win);
                }
                menu_win = create_newwin(12, 30, (LINES - 12)/2, (COLS - 30)/2);
                break;
            case 3:
            initializeGrid(grid1);
            initializeGrid(grid2);
            placeShips(grid1);
            placeShips(grid2);
            resetShotsMade();  // Reset shotsMade when redeploying ships
            mvwprintw(menu_win, 10, 2, "Ships are redeployed. Press any key...");
            wrefresh(menu_win);
            wgetch(menu_win);
            break;
            case 4:
                if (loadGame("saved_game.dat")) {
                    main_win = create_newwin(WINDOW_HEIGHT, WINDOW_WIDTH, 0, 0);
                    mvwprintw(main_win, 1, 2, "Parent's Grid");
                    mvwprintw(main_win, 1, WINDOW_WIDTH / 2 + 2, "Child's Grid");
                    
                    while (1) {
                        mvwprintw(main_win, WINDOW_HEIGHT-3, 2, "                                        ");
                        mvwprintw(main_win, WINDOW_HEIGHT-2, 2, "                                        ");

                        printGridNcurses(main_win, grid2, 2, 2, 1);
                        printGridNcurses(main_win, grid1, 2, WINDOW_WIDTH/2 + 2, 1);
                        
                        if (currentPlayer == 0) {
                            mvwprintw(main_win, WINDOW_HEIGHT-4, 2, "Parent's turn:");
                            wrefresh(main_win);
                            custom_delay(DELAY_BETWEEN_TURNS);
                            
                            mvwprintw(main_win, WINDOW_HEIGHT-4, 2, "Parent's turn: Thinking...");
                            wrefresh(main_win);
                            turnAI(main_win, grid1, "Parent");
                            if (allShipsSunk(grid1)) {
                                mvwprintw(main_win, WINDOW_HEIGHT-2, 2, "Parent wins!");
                                wrefresh(main_win);
                                custom_delay(DELAY_BETWEEN_TURNS * 3);
                                break;
                            }
                        } else {
                            custom_delay(DELAY_BETWEEN_TURNS);
                            mvwprintw(main_win, WINDOW_HEIGHT-4, 2, "Child's turn:");
                            wrefresh(main_win);
                            custom_delay(DELAY_BETWEEN_TURNS);
                            
                            mvwprintw(main_win, WINDOW_HEIGHT-4, 2, "Child's turn: Thinking...");
                            wrefresh(main_win);
                            turnAI(main_win, grid2, "Child");
                            if (allShipsSunk(grid2)) {
                                mvwprintw(main_win, WINDOW_HEIGHT-2, 2, "Child wins!");
                                wrefresh(main_win);
                                custom_delay(DELAY_BETWEEN_TURNS * 3);
                                break;
                            }
                        }

                        wrefresh(main_win);
                        custom_delay(DELAY_BETWEEN_TURNS);
                        currentPlayer = (currentPlayer + 1) % 2;
                    }
                    delwin(main_win);
                }
                break;
            case 5:
                cleanup_ncurses();
                return 0;    
            default:
                mvwprintw(menu_win, 10, 2, "Invalid choice. Please enter a valid key");
                wrefresh(menu_win);
                wgetch(menu_win);
        }
    }
 
    cleanup_ncurses();
    return 0;
}