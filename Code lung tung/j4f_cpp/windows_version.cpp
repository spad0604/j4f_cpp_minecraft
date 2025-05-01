#include <windows.h>
#include <conio.h>
#include <stdio.h>
#include <stdint.h>
#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define X_PIXELS 400
#define Y_PIXELS 200

#define X_BLOCK 20
#define Y_BLOCK 20
#define Z_BLOCK 10

#define VIEW_HEIGH 0.5
#define VIEW_WIDTH 0.5

#define BLOCK_BORDER_SIZE 0.5

typedef struct Vector
{
    float x;
    float y;
    float z;
} vect;

typedef struct Vector2
{
    float psi;
    float phi;
} vect2;

typedef struct Vector_vector2
{
    vect pos;
    vect2 view;
} player_pos_view;

HANDLE hConsole;
CONSOLE_CURSOR_INFO oldCursorInfo;
DWORD oldConsoleMode;

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

static char keystate[256] = {0}; // Array to store key states

void process_input()
{
    memset(keystate, 0, sizeof(keystate)); // Reset key states

    // Check for key presses
    while (_kbhit())
    {
        char c = _getch();
        std::cout << "input: " << c << std::endl;
        unsigned char key = (unsigned char)c;
        keystate[key] = 1; // Mark key as pressed
        if (c == 'q')
        {
            exit(0);
        }
    }
}

int is_key_pressed(char key)
{
    unsigned char k = (unsigned char)key;
    return keystate[k]; // Return key state
}

char **init_picture()
{
    // Create a 2D array to store ASCII image
    char **picture = (char **)malloc(sizeof(char *) * Y_PIXELS);

    for (int i = 0; i < Y_PIXELS; i++)
    {
        picture[i] = (char *)malloc(sizeof(char) * X_PIXELS);
    }
    return picture;
}

char ***init_block()
{
    // Create a 3D array to store blocks
    char ***blocks = (char ***)malloc(sizeof(char **) * Z_BLOCK);
    for (int i = 0; i < Z_BLOCK; i++)
    {
        blocks[i] = (char **)malloc(sizeof(char *) * Y_BLOCK);
        for (int j = 0; j < Y_BLOCK; j++)
        {
            blocks[i][j] = (char *)malloc(sizeof(char) * X_BLOCK);
            for (int k = 0; k < X_BLOCK; k++)
            {
                blocks[i][j][k] = ' ';
            }
        }
    }
    return blocks;
}

vect angles_to_vect(vect2 angles)
{
    vect res;
    res.x = cos(angles.psi) * cos(angles.phi);
    res.y = cos(angles.psi) * sin(angles.phi);
    res.z = sin(angles.psi);
    return res;
}

vect vect_add(vect v1, vect v2) // Add two vectors
{
    vect sum_vect;
    sum_vect.x = v1.x + v2.x;
    sum_vect.y = v1.y + v2.y;
    sum_vect.z = v1.z + v2.z;

    return sum_vect;
}

vect vect_sub(vect v1, vect v2) // Subtract two vectors
{
    vect sub_vect;
    sub_vect.x = v1.x - v2.x;
    sub_vect.y = v1.y - v2.y;
    sub_vect.z = v1.z - v2.z;

    return sub_vect;
}

vect vect_scale(float scale, vect v) // Multiply a vector by a scalar
{
    vect res = {scale * v.x, scale * v.y, scale * v.z};
    return res;
}

void vect_normalize(vect *v)
{
    float len = sqrt(v->x * v->x + v->y * v->y + v->z * v->z);
    v->x /= len;
    v->y /= len;
    v->z /= len;
}

vect **init_directions(vect2 view)
{
    view.psi -= VIEW_HEIGH / 2.0;
    vect screen_down = angles_to_vect(view);
    view.psi += VIEW_HEIGH;
    vect screen_up = angles_to_vect(view);
    view.psi -= VIEW_HEIGH / 2.0;
    view.phi -= VIEW_WIDTH / 2.0;
    vect screen_left = angles_to_vect(view);
    view.phi += VIEW_WIDTH;
    vect screen_right = angles_to_vect(view);
    view.phi -= VIEW_WIDTH / 2.0;

    vect screen_mid_vert = vect_scale(0.5, vect_add(screen_up, screen_down));
    vect screen_mid_hor = vect_scale(0.5, vect_add(screen_left, screen_right));
    vect mid_to_left = vect_sub(screen_left, screen_mid_hor);
    vect mid_to_up = vect_sub(screen_up, screen_mid_vert);

    vect **dir = (vect **)malloc(sizeof(vect *) * Y_PIXELS);
    for (int i = 0; i < Y_PIXELS; i++)
    {
        dir[i] = (vect *)malloc(sizeof(vect) * X_PIXELS);
    }
    for (int y_pix = 0; y_pix < Y_PIXELS; y_pix++)
    {
        for (int x_pix = 0; x_pix < X_PIXELS; x_pix++)
        {
            vect tmp = vect_add(vect_add(screen_mid_hor, mid_to_left), mid_to_up);
            tmp = vect_sub(tmp, vect_scale(((float)x_pix / (X_PIXELS - 1)) * 2, mid_to_left));
            tmp = vect_sub(tmp, vect_scale(((float)y_pix / (Y_PIXELS - 1)) * 2, mid_to_up));
            vect_normalize(&tmp);
            dir[y_pix][x_pix] = tmp;
        }
    }
    return dir;
}

int ray_outside(vect pos)
{
    if (pos.x >= X_BLOCK || pos.y >= Y_BLOCK || pos.z >= Z_BLOCK || pos.x < 0 || pos.y < 0 || pos.z < 0)
    {
        return 1;
    }
    return 0;
}

int on_block_border(vect pos)
{
    int cnt = 0;
    if (fabsf(pos.x - roundf(pos.x)) < BLOCK_BORDER_SIZE)
    {
        cnt++;
    }
    if (fabsf(pos.y - roundf(pos.y)) < BLOCK_BORDER_SIZE)
    {
        cnt++;
    }
    if (fabsf(pos.z - roundf(pos.z)) < BLOCK_BORDER_SIZE)
    {
        cnt++;
    }
    if (cnt >= 2)
    {
        return 1;
    }
    return 0;
}

char raytrace(vect pos, vect dir, char ***blocks)
{
    float eps = 0.01;
    while (!ray_outside(pos))
    {
        char c = blocks[(int)pos.z][(int)pos.y][(int)pos.x];
        if (c != ' ')
        {
            if (on_block_border(pos))
            {
                return '@';
            }
            else
            {
                return c;
            }
        }
        float dist = 2;
        if (dir.x > eps)
        {
            dist = std::min(dist, ((int)(pos.x + 1) - pos.x) / dir.x);
        }
        else if (dir.x < -eps)
        {
            dist = std::min(dist, ((int)pos.x - pos.x) / dir.x);
        }
        if (dir.y > eps)
        {
            dist = std::min(dist, ((int)(pos.y + 1) - pos.y) / dir.y);
        }
        else if (dir.y < -eps)
        {
            dist = std::min(dist, ((int)pos.y - pos.y) / dir.y);
        }
        if (dir.z > eps)
        {
            dist = std::min(dist, ((int)(pos.z + 1) - pos.z) / dir.z);
        }
        else if (dir.z < -eps)
        {
            dist = std::min(dist, ((int)pos.z - pos.z) / dir.z);
        }
        pos = vect_add(pos, vect_scale(dist + eps, dir));
    }
    return ' ';
}

void get_picture(char **picture, player_pos_view posview, char ***blocks)
{
    vect **directions = init_directions(posview.view);

    for (int y = 0; y < Y_PIXELS; y++)
    {
        for (int x = 0; x < X_PIXELS; x++)
        {
            picture[y][x] = raytrace(posview.pos, directions[y][x], blocks);
        }
    }

    // Free memory
    for (int i = 0; i < Y_PIXELS; i++)
    {
        free(directions[i]);
    }
    free(directions);
}

void draw_ascii(char **picture)
{
    // Move cursor to top-left corner
    COORD cursorPosition = {0, 0};
    SetConsoleCursorPosition(hConsole, cursorPosition);

    for (int i = 0; i < Y_PIXELS; i++)
    {
        for (int j = 0; j < X_PIXELS; j++)
        {
            std::cout << picture[i][j];
        }
        std::cout << std::endl;
    }
}

player_pos_view init_posview()
{
    player_pos_view posview;

    posview.pos.x = 5;
    posview.pos.y = 5;
    posview.pos.z = 5;
    posview.view.psi = 0;
    posview.view.phi = 0;

    return posview;
}

int main()
{
    init_terminal();

    char **picture = init_picture();
    char ***blocks = init_block();

    // Initialize blocks
    for (int x = 0; x < X_BLOCK; x++)
    {
        for (int y = 0; y < Y_BLOCK; y++)
        {
            for (int z = 0; z < Z_BLOCK; z++)
            {
                blocks[z][y][x] = '@';
            }
        }
    }

    player_pos_view posview = init_posview();

    while (1)
    {
        process_input();
        Sleep(20); // Windows equivalent of usleep(20000)

        if (is_key_pressed('q'))
        {
            exit(0);
        }

        get_picture(picture, posview, blocks);
        draw_ascii(picture);
    }

    // Free memory
    for (int i = 0; i < Y_PIXELS; i++)
    {
        free(picture[i]);
    }
    free(picture);

    for (int i = 0; i < Z_BLOCK; i++)
    {
        for (int j = 0; j < Y_BLOCK; j++)
        {
            free(blocks[i][j]);
        }
        free(blocks[i]);
    }
    free(blocks);

    restore_terminal();

    return 0;
}