#include <fcntl.h>
#include <windows.h>
#include <conio.h>
#include <stdint.h>
#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <thread>
#include <vector>
#include <mutex>
#include <atomic>
#include <condition_variable>

#define Y_PIXELS 180
#define X_PIXELS 900
#define Z_BLOCKS 10
#define Y_BLOCKS 20
#define EYE_HEIGHT 1.5
#define X_BLOCKS 20
#define VIEW_HEIGHT 0.7
#define VIEW_WIDTH 1
#define BLOCK_BORDER_SIZE 0.05
#define NUM_THREADS 8  // Number of threads to use for ray calculations

HANDLE hConsole;
CONSOLE_CURSOR_INFO oldCursorInfo;
DWORD oldConsoleMode;
std::mutex pictureMutex;  // Mutex for thread-safe access to the picture array

typedef struct Vector {
    float x;
    float y;
    float z;
} vect;

typedef struct Vector2 {
    float psi;
    float phi;
} vect2;

typedef struct Vector_vector2 {
    vect pos;
    vect2 view;
} player_pos_view;

// Thread pool implementation
class ThreadPool {
private:
    std::vector<std::thread> workers;
    std::vector<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    std::atomic<bool> stop;

public:
    ThreadPool(size_t threads) : stop(false) {
        for (size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock, [this] { 
                            return this->stop || !this->tasks.empty(); 
                        });
                        
                        if (this->stop && this->tasks.empty()) {
                            return;
                        }
                        
                        task = std::move(this->tasks.back());
                        this->tasks.pop_back();
                    }
                    task();
                }
            });
        }
    }

    template<class F>
    void enqueue(F&& f) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            tasks.emplace_back(std::forward<F>(f));
        }
        condition.notify_one();
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        
        condition.notify_all();
        
        for (std::thread &worker : workers) {
            worker.join();
        }
    }
};

void init_terminal()
{
    // Get and store the console handle
    hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    // Hide cursor
    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(hConsole, &oldCursorInfo);
    cursorInfo.dwSize = 1;
    cursorInfo.bVisible = FALSE;
    SetConsoleCursorInfo(hConsole, &cursorInfo);

    // Set console mode to process keyboard input
    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hInput, &oldConsoleMode);
    SetConsoleMode(hInput, ENABLE_PROCESSED_INPUT);

    // Clear the screen
    system("cls");
}

void restore_terminal()
{
    // Restore cursor
    SetConsoleCursorInfo(hConsole, &oldCursorInfo);

    // Restore console mode
    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    SetConsoleMode(hInput, oldConsoleMode);

    std::cout << "terminal restored" << std::endl;
}

static char keystate[256] = { 0 };

void process_input()
{
    memset(keystate, 0, sizeof(keystate)); // Reset key states

    // Check for key presses
    while (_kbhit())
    {
        char c = _getch();
        unsigned char key = (unsigned char)c;
        keystate[key] = 1; // Mark key as pressed
        if (c == 'q')
        {
            exit(0);
        }
    }
}

int is_key_pressed(char key) {
    return keystate[(unsigned char)key];
}

char** init_picture() {
    char** picture = (char**)malloc(sizeof(char*) * Y_PIXELS);
    for (int i = 0; i < Y_PIXELS; i++) {
        picture[i] = (char*)malloc(sizeof(char) * X_PIXELS);
        memset(picture[i], ' ', X_PIXELS); // Initialize with spaces
    }
    return picture;
}

char*** init_blocks() {
    char*** blocks = (char***)malloc(sizeof(char**) * Z_BLOCKS);
    for (int i = 0; i < Z_BLOCKS; i++) {
        blocks[i] = (char**)malloc(sizeof(char*) * Y_BLOCKS);
        for (int j = 0; j < Y_BLOCKS; j++) {
            blocks[i][j] = (char*)malloc(sizeof(char) * X_BLOCKS);
            for (int k = 0; k < X_BLOCKS; k++) {
                blocks[i][j][k] = ' ';
            }
        }
    }
    return blocks;
}

player_pos_view init_posview() {
    player_pos_view posview;
    posview.pos.x = 5;
    posview.pos.y = 5;
    posview.pos.z = 4 + EYE_HEIGHT;
    posview.view.phi = 0;
    posview.view.psi = 0;
    return posview;
}

vect angles_to_vect(vect2 angles) {
    vect res;
    res.x = cos(angles.psi) * cos(angles.phi);
    res.y = cos(angles.psi) * sin(angles.phi);
    res.z = sin(angles.psi);
    return res;
}

vect vect_add(vect v1, vect v2) {
    vect res;
    res.x = v1.x + v2.x;
    res.y = v1.y + v2.y;
    res.z = v1.z + v2.z;
    return res;
}

vect vect_scale(float s, vect v) {
    vect res = { s * v.x, s * v.y, s * v.z };
    return res;
}

vect vect_sub(vect v1, vect v2) {
    vect v3 = vect_scale(-1, v2);
    return vect_add(v1, v3);
}

void vect_normalize(vect* v) {
    float len = sqrt(v->x * v->x + v->y * v->y + v->z * v->z);
    v->x /= len;
    v->y /= len;
    v->z /= len;
}

vect** init_directions(vect2 view) {
    view.psi -= VIEW_HEIGHT / 2.0;
    vect screen_down = angles_to_vect(view);
    view.psi += VIEW_HEIGHT;
    vect screen_up = angles_to_vect(view);
    view.psi -= VIEW_HEIGHT / 2.0;
    view.phi -= VIEW_WIDTH / 2.0;
    vect screen_left = angles_to_vect(view);
    view.phi += VIEW_WIDTH;
    vect screen_right = angles_to_vect(view);
    view.phi -= VIEW_WIDTH / 2.0;

    vect screen_mid_vert = vect_scale(0.5, vect_add(screen_up, screen_down));
    vect screen_mid_hor = vect_scale(0.5, vect_add(screen_left, screen_right));
    vect mid_to_left = vect_sub(screen_left, screen_mid_hor);
    vect mid_to_up = vect_sub(screen_up, screen_mid_vert);

    vect** dir = (vect**)malloc(sizeof(vect*) * Y_PIXELS);
    for (int i = 0; i < Y_PIXELS; i++) {
        dir[i] = (vect*)malloc(sizeof(vect) * X_PIXELS);
    }
    for (int y_pix = 0; y_pix < Y_PIXELS; y_pix++) {
        for (int x_pix = 0; x_pix < X_PIXELS; x_pix++) {
            vect tmp = vect_add(vect_add(screen_mid_hor, mid_to_left), mid_to_up);
            tmp = vect_sub(tmp, vect_scale(((float)x_pix / (X_PIXELS - 1)) * 2, mid_to_left));
            tmp = vect_sub(tmp, vect_scale(((float)y_pix / (Y_PIXELS - 1)) * 2, mid_to_up));
            vect_normalize(&tmp);
            dir[y_pix][x_pix] = tmp;
        }
    }
    return dir;
}

int ray_outside(vect pos) {
    if (pos.x >= X_BLOCKS || pos.y >= Y_BLOCKS || pos.z >= Z_BLOCKS
        || pos.x < 0 || pos.y < 0 || pos.z < 0) {
        return 1;
    }
    return 0;
}

int on_block_border(vect pos) {
    int cnt = 0;
    if (fabsf(pos.x - roundf(pos.x)) < BLOCK_BORDER_SIZE) {
        cnt++;
    }
    if (fabsf(pos.y - roundf(pos.y)) < BLOCK_BORDER_SIZE) {
        cnt++;
    }
    if (fabsf(pos.z - roundf(pos.z)) < BLOCK_BORDER_SIZE) {
        cnt++;
    }
    if (cnt >= 2) {
        return 1;
    }
    return 0;
}

char raytrace(vect pos, vect dir, char*** blocks) {
    float eps = 0.01;
    while (!ray_outside(pos)) {
        char c = blocks[(int)pos.z][(int)pos.y][(int)pos.x];
        if (c != ' ') {
            if (on_block_border(pos)) {
                return '-';
            }
            else {
                return c;
            }
        }
        float dist = 2;
        if (dir.x > eps) {
            dist = std::min(dist, ((int)(pos.x + 1) - pos.x) / dir.x);
        }
        else if (dir.x < -eps) {
            dist = std::min(dist, ((int)pos.x - pos.x) / dir.x);
        }
        if (dir.y > eps) {
            dist = std::min(dist, ((int)(pos.y + 1) - pos.y) / dir.y);
        }
        else if (dir.y < -eps) {
            dist = std::min(dist, ((int)pos.y - pos.y) / dir.y);
        }
        if (dir.z > eps) {
            dist = std::min(dist, ((int)(pos.z + 1) - pos.z) / dir.z);
        }
        else if (dir.z < -eps) {
            dist = std::min(dist, ((int)pos.z - pos.z) / dir.z);
        }
        pos = vect_add(pos, vect_scale(dist + eps, dir));
    }
    return ' ';
}

// Function to render a chunk of the picture (used by worker threads)
void render_chunk(int start_y, int end_y, int start_x, int end_x, 
                  char** picture, vect** directions, 
                  player_pos_view posview, char*** blocks) {
    for (int y = start_y; y < end_y; y++) {
        for (int x = start_x; x < end_x; x++) {
            char pixel = raytrace(posview.pos, directions[y][x], blocks);
            
            // Thread-safe update to the picture array
            std::lock_guard<std::mutex> lock(pictureMutex);
            picture[y][x] = pixel;
        }
    }
}

void get_picture_multithreaded(char** picture, player_pos_view posview, char*** blocks) {
    vect** directions = init_directions(posview.view);
    
    // Create thread pool
    ThreadPool pool(NUM_THREADS);
    
    // Calculate the size of chunks for each thread
    int chunk_height = Y_PIXELS / NUM_THREADS;
    
    // Submit tasks to thread pool
    for (int i = 0; i < NUM_THREADS; i++) {
        int start_y = i * chunk_height;
        int end_y = (i == NUM_THREADS - 1) ? Y_PIXELS : (i + 1) * chunk_height;
        
        pool.enqueue([start_y, end_y, picture, directions, posview, blocks]() {
            render_chunk(start_y, end_y, 0, X_PIXELS, picture, directions, posview, blocks);
        });
    }
    
    // Pool destructor will wait for all tasks to complete
    
    // Free directions array
    for (int i = 0; i < Y_PIXELS; i++) {
        free(directions[i]);
    }
    free(directions);
}

void draw_ascii(char** picture) {
    fflush(stdout);
    printf("\033[0;0H");
    for (int i = 0; i < Y_PIXELS; i++) {
        int current_color = 0;
        for (int j = 0; j < X_PIXELS; j++) {
            if (picture[i][j] == 'o' && current_color != 32) {
                printf("\x1B[32m");
                current_color = 32;
            }
            else if (picture[i][j] != 'o' && current_color != 0) {
                printf("\x1B[0m");
                current_color = 0;
            }
            printf("%c", picture[i][j]);
        }
        printf("\x1B[0m\n");
    }
}

void update_pos_view(player_pos_view* posview, char*** blocks) {
    float move_eps = 0.30;
    float tilt_eps = 0.1;
    int x = (int)posview->pos.x, y = (int)posview->pos.y;
    int z = (int)posview->pos.z - EYE_HEIGHT + 0.01;
    if (blocks[z][y][x] != ' ') {
        posview->pos.z += 1;
    }
    z = (int)posview->pos.z - EYE_HEIGHT - 0.01;
    if (blocks[z][y][x] == ' ') {
        posview->pos.z -= 1;
    }

    if (is_key_pressed('w')) {
        posview->view.psi += tilt_eps;
    }
    if (is_key_pressed('s')) {
        posview->view.psi -= tilt_eps;
    }
    if (is_key_pressed('d')) {
        posview->view.phi += tilt_eps;
    }
    if (is_key_pressed('a')) {
        posview->view.phi -= tilt_eps;
    }
    vect direction = angles_to_vect(posview->view);
    if (is_key_pressed('i')) {
        posview->pos.x += move_eps * direction.x;
        posview->pos.y += move_eps * direction.y;
    }
    if (is_key_pressed('k')) {
        posview->pos.x -= move_eps * direction.x;
        posview->pos.y -= move_eps * direction.y;
    }
    if (is_key_pressed('j')) {
        posview->pos.x += move_eps * direction.y;
        posview->pos.y -= move_eps * direction.x;
    }
    if (is_key_pressed('l')) {
        posview->pos.x -= move_eps * direction.y;
        posview->pos.y += move_eps * direction.x;
    }
}

vect get_current_block(player_pos_view posview, char*** blocks) {
    vect pos = posview.pos;
    vect dir = angles_to_vect(posview.view);
    float eps = 0.01;
    while (!ray_outside(pos)) {
        char c = blocks[(int)pos.z][(int)pos.y][(int)pos.x];
        if (c != ' ') {
            return pos;
        }
        float dist = 2;
        if (dir.x > eps) {
            dist = std::min(dist, ((int)(pos.x + 1) - pos.x) / dir.x);
        }
        else if (dir.x < -eps) {
            dist = std::min(dist, ((int)pos.x - pos.x) / dir.x);
        }
        if (dir.y > eps) {
            dist = std::min(dist, ((int)(pos.y + 1) - pos.y) / dir.y);
        }
        else if (dir.y < -eps) {
            dist = std::min(dist, ((int)pos.y - pos.y) / dir.y);
        }
        if (dir.z > eps) {
            dist = std::min(dist, ((int)(pos.z + 1) - pos.z) / dir.z);
        }
        else if (dir.z < -eps) {
            dist = std::min(dist, ((int)pos.z - pos.z) / dir.z);
        }
        pos = vect_add(pos, vect_scale(dist + eps, dir));
    }
    return pos;
}

void place_block(vect pos, char*** blocks, char block) {
    int x = (int)pos.x, y = (int)pos.y, z = (int)pos.z;
    float dists[6];
    dists[0] = fabsf(x + 1 - pos.x);
    dists[1] = fabsf(pos.x - x);
    dists[2] = fabsf(y + 1 - pos.y);
    dists[3] = fabsf(pos.y - y);
    dists[4] = fabsf(z + 1 - pos.z);
    dists[5] = fabsf(pos.z - z);
    int min = 0;
    float mindist = dists[0];
    for (int i = 0; i < 6; i++) {
        if (dists[i] < mindist) {
            mindist = dists[i];
            min = i;
        }
    }
    switch (min) {
    case 0:
        blocks[z][y][x + 1] = block;
        break;
    case 1:
        blocks[z][y][x - 1] = block;
        break;
    case 2:
        blocks[z][y + 1][x] = block;
        break;
    case 3:
        blocks[z][y - 1][x] = block;
        break;
    case 4:
        blocks[z + 1][y][x] = block;
        break;
    case 5:
        blocks[z - 1][y][x] = block;
        break;
    default:
        break;
    }
}

int main() {
    init_terminal();
    char** picture = init_picture();
    char*** blocks = init_blocks();
    
    // Create ground level
    for (int x = 0; x < X_BLOCKS; x++) {
        for (int y = 0; y < Y_BLOCKS; y++) {
            for (int z = 0; z < 4; z++) {
                blocks[z][y][x] = '@';
            }
        }
    }
    
    player_pos_view posview = init_posview();
    
    // Display performance info
    std::cout << "Multithreaded raycasting engine using " << NUM_THREADS << " threads" << std::endl;
    
    // Main game loop
    while (1) {
        // Process keyboard input
        process_input();
        if (is_key_pressed('q')) {
            break;
        }
        
        // Update player position and view
        update_pos_view(&posview, blocks);
        
        // Handle block interaction
        vect current_block = get_current_block(posview, blocks);
        int have_current_block = !ray_outside(current_block);
        int current_block_x = current_block.x;
        int current_block_y = current_block.y;
        int current_block_z = current_block.z;
        char current_block_c;
        int removed = 0;
        
        if (have_current_block) {
            current_block_c = blocks[current_block_z][current_block_y][current_block_x];
            blocks[current_block_z][current_block_y][current_block_x] = 'o';
            
            // Remove block with 'x' key
            if (is_key_pressed('x')) {
                removed = 1;
                blocks[current_block_z][current_block_y][current_block_x] = ' ';
            }

            // Place block with space key
            if (is_key_pressed(' ')) {
                place_block(current_block, blocks, '@');
            }
        }

        // Render the scene using multithreading
        get_picture_multithreaded(picture, posview, blocks);
        
        // Restore the highlighted block
        if (have_current_block && !removed) {
            blocks[current_block_z][current_block_y][current_block_x] = current_block_c;
        }
        
        // Draw the scene
        draw_ascii(picture);
        
        // Small delay to limit frame rate
        Sleep(20);
    }
    
    // Clean up
    restore_terminal();
    
    // Free memory
    for (int i = 0; i < Y_PIXELS; i++) {
        free(picture[i]);
    }
    free(picture);
    
    for (int i = 0; i < Z_BLOCKS; i++) {
        for (int j = 0; j < Y_BLOCKS; j++) {
            free(blocks[i][j]);
        }
        free(blocks[i]);
    }
    free(blocks);
    
    return 0;
}