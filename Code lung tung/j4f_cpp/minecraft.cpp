#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <termios.h>
#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define X_PIXELS 40
#define Y_PIXELS 20

#define X_BLOCK 20
#define Y_BLOCK 20
#define Z_BLOCK 10

#define VIEW_HEIGH 0.5
#define VIEW_WIDTH 0.5

static struct termios old_terminos, new_terminos;

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

void init_terminal()
{
    tcgetattr(STDIN_FILENO, &old_terminos); // đúng tên hàm

    new_terminos = old_terminos;
    new_terminos.c_lflag &= ~(ICANON | ECHO); // Disable canonical mode and echo
    tcsetattr(STDIN_FILENO, TCSANOW, &new_terminos);
    fflush(stdout);
}

void restore_terminal()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &old_terminos);
    std::cout << "terminal restored" << std::endl;
}

static char keystate[256] = {0}; // mảng lưu trạng thái phím

void process_input()
{ // đọc phím nhấn
    char c;
    memset(keystate, 0, sizeof(keystate)); // reset trạng thái phím

    while (read(STDIN_FILENO, &c, 1) > 0)
    {
        std::cout << "input: " << c << std::endl;
        unsigned char key = (unsigned char)c;
        keystate[key] = 1; // đánh dấu phím đã được nhấn
        if (c == 'q')
        {
            exit(0);
        }
    }
}

int is_key_pressed(char key)
{ // kiểm tra trạng thái phím
    unsigned char k = (unsigned char)key;
    return keystate[k]; // trả về trạng thái phím
}

char **init_picture()
{ // Khởi tạo khung hình
    // Tạo mảng 2 chiều để lưu ảnh ký tự ascii
    char **picture = (char **)malloc(sizeof(char *) * Y_PIXELS);

    for (int i = 0; i < Y_PIXELS; i++)
    {
        picture[i] = (char *)malloc(sizeof(char) * X_PIXELS);
    }
    return picture;
}

char ***init_block()
{
    // Tạo mảng 3 chiều để lưu các khối
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

vect angles_to_vect(vect2 angles) // Chuyển đổi góc sang vector đơn vị hướng nhìn trong không gian 3D
{
    // Đây là công thức chuyển từ tọa độ cầu (spherical coordinates) sang tọa độ Đề-các (cartesian coordinates).
    vect dir;

    dir.x = cos(angles.phi) * sin(angles.psi);
    dir.y = sin(angles.phi);
    dir.z = cos(angles.phi) * cos(angles.psi);

    return dir;
}

vect vect_add(vect v1, vect v2) // Cộng 2 vector
{
    vect sum_vect;
    sum_vect.x = v1.x + v2.x;
    sum_vect.y = v1.y + v2.y;
    sum_vect.z = v1.z + v2.z;

    return sum_vect;
}

vect vect_sub(vect v1, vect v2) // Trừ 2 vector
{
    vect sub_vect;
    sub_vect.x = v1.x - v2.x;
    sub_vect.y = v1.y - v2.y;
    sub_vect.z = v1.z - v2.z;

    return sub_vect;
}

vect vect_scale(float scale, vect v) // Nhân vector với một số thực
{
    vect scaled_vect;
    scaled_vect.x = v.x * scale;
    scaled_vect.y = v.y * scale;
    scaled_vect.z = v.z * scale;

    return scaled_vect;
}

void vect_normalize(vect *v) // Chuyển đổi vector về vector đơn vị
{
    float len = sqrt(v->x * v->x + v->y * v->y + v->z * v->z);
    v->x /= len;
    v->y /= len;
    v->z /= len;
}

vect **init_directions(vect2 view)
{
    view.psi = VIEW_HEIGH / 2.0;

    vect screen_down = angles_to_vect(view); // Tính toán hướng nhìn xuống
    view.psi += VIEW_HEIGH;

    vect screen_up = angles_to_vect(view); // Tính toán hướng nhìn lên
    view.psi -= VIEW_HEIGH / 2.0;
    view.phi -= VIEW_WIDTH / 2.0;

    vect screen_left = angles_to_vect(view); // Tính toán hướng nhìn sang trái
    view.phi += VIEW_WIDTH;

    vect screen_right = angles_to_vect(view); // Tính toán hướng nhìn sang phải
    view.phi += VIEW_WIDTH / 2.0;

    vect screen_mid_vert = vect_scale(0.5, vect_add(screen_down, screen_up)); // Tính toán hướng nhìn giữa
    vect screen_mid_horiz = vect_scale(0.5, vect_add(screen_left, screen_right));

    vect mid_to_left = vect_sub(screen_left, screen_mid_horiz); // vector từ giữa đến mép trái (dịch ngang)
    vect mid_to_up = vect_sub(screen_up, screen_mid_vert);      //  vector từ giữa đến đỉnh (dịch dọc)
    vect **dir = (vect **)malloc(sizeof(vect *) * Y_PIXELS);

    for (int i = 0; i < Y_PIXELS; i++)
    {
        dir[i] = (vect *)malloc(sizeof(vect) * X_PIXELS);
    }

    for (int y_pix = 0; y_pix < Y_PIXELS; y_pix++)
    {
        for (int x_pix = 0; x_pix < X_PIXELS; x_pix++)
        {
            vect tmp = vect_add(vect_add(screen_mid_horiz, mid_to_left), mid_to_up);
            tmp = vect_sub(tmp, vect_scale(((float)x_pix / (X_PIXELS - 1)) * 2, mid_to_left));
            tmp = vect_sub(tmp, vect_scale(((float)y_pix / (Y_PIXELS - 1)) * 2, mid_to_up));

            vect_normalize(&tmp);

            dir[y_pix][x_pix] = tmp;
        }
    }

    return dir;
}

char raytrace(vect pos, vect dir, char*** blocks)
{ // Bắn tia nhìn xem tia sẽ chạm vào khối nào
    return 'b';
}

char **get_picture(char **picture, player_pos_view posview, char ***blocks)
{
    vect **directions = init_directions(posview.view);

    for (int y = 0; y < Y_PIXELS; y++)
    {
        for (int x = 0; x < X_PIXELS; x++)
        {
            picture[y][x] = raytrace(posview.pos, directions[y][x], blocks);
        }
    }
}

void draw_ascii(char **picture)
{
    fflush(stdout);

    for (int i = 0; i < Y_PIXELS; i++)
    {
        for (int j = 0; j < X_PIXELS; j++)
        {
            std::cout << picture[i][j];
        }
        std::cout << std::endl;
    }
}

player_pos_view init_postview()
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

    player_pos_view posview = init_postview();

    while (1)
    {
        process_input();
        usleep(20000);

        if (is_key_pressed('q'))
        {
            restore_terminal();
            exit(0);
        }

        get_picture(picture, posview, blocks);

        draw_ascii(picture);
    }

    restore_terminal();

    return 0;
}