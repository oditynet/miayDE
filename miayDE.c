#include <X11/Xlib.h>
#include <sys/stat.h>
#include <X11/Xutil.h>
#include <X11/Xos.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <shadow.h>
#include <crypt.h>
#include <security/pam_appl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <grp.h>
#include <ctype.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <dbus/dbus.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <math.h>

#define MAX_USERS 20
#define AVATAR_SIZE 80
#define SESSION_NAME_MAX 32
#define FPS 60

typedef struct {
    char username[32];
    char display_name[64];
    int uid;
    int selected;
} User;

typedef struct {
    char name[SESSION_NAME_MAX];
    char exec[64];
} Session;

typedef struct {
    Display *display;
    Window window;
    GC gc;
    int screen;
    int width, height;
    User users[MAX_USERS];
    int user_count;
    int selected_user;
    char password[64];
    int password_active;
    XFontStruct *font;
    Session sessions[20];
    int session_count;
    int selected_session;
    int show_sessions;
    pid_t xserver_pid;
    pid_t dbus_pid;
    int mouse_x;
    int mouse_y;
    int mouse_buttons;
    char dbus_address[256];
    char error_message[256];
    char warning_message[256];
    time_t error_time;
    time_t warning_time;
    int show_error;
    int show_warning;
    int password_focus;
} DisplayManager;

// Градиентные цвета
#define COLOR_BG1 0xf61a2e
#define COLOR_BG2 0x16213e
#define COLOR_ACCENT1 0x0f3460
#define COLOR_ACCENT2 0xe94560
#define COLOR_TEXT 0xf0f0f0
#define COLOR_HIGHLIGHT 0x4cc9f0
#define COLOR_USER_BG 0x2d2d4d
#define COLOR_USER_SELECTED 0x4a4a8a
#define COLOR_PASS_BG 0x3d3d6d
#define COLOR_PASS_FOCUS 0x5a5a9a

// PAM conversation function
static int conversation(int num_msg, const struct pam_message **msg,
                       struct pam_response **resp, void *appdata_ptr) {
    if (num_msg <= 0 || num_msg > PAM_MAX_NUM_MSG) {
        return PAM_CONV_ERR;
    }
    
    struct pam_response *response = calloc(num_msg, sizeof(struct pam_response));
    if (response == NULL) {
        return PAM_BUF_ERR;
    }
    
    for (int i = 0; i < num_msg; i++) {
        switch (msg[i]->msg_style) {
            case PAM_PROMPT_ECHO_OFF:
            case PAM_PROMPT_ECHO_ON:
                response[i].resp = strdup((char*)appdata_ptr);
                response[i].resp_retcode = 0;
                break;
            case PAM_ERROR_MSG:
            case PAM_TEXT_INFO:
                break;
            default:
                free(response);
                return PAM_CONV_ERR;
        }
    }
    
    *resp = response;
    return PAM_SUCCESS;
}

int get_users(User *users) {
    setpwent();
    struct passwd *p;
    int count = 0;
    
    while ((p = getpwent()) != NULL && count < MAX_USERS) {
        if (p->pw_uid >= 1000 && strcmp(p->pw_name, "nobody") != 0) {
            strcpy(users[count].username, p->pw_name);
            if (p->pw_gecos && *p->pw_gecos) {
                strncpy(users[count].display_name, p->pw_gecos, 63);
            } else {
                strncpy(users[count].display_name, p->pw_name, 63);
            }
            users[count].display_name[63] = '\0';
            users[count].uid = p->pw_uid;
            users[count].selected = 0;
            count++;
        }
    }
    endpwent();
    return count;
}

int get_sessions(Session *sessions) {
    int count = 0;
    
    if (access("/usr/bin/gnome-session", F_OK) == 0) {
        strcpy(sessions[count].name, "GNOME");
        strcpy(sessions[count].exec, "gnome-session");
        count++;
    }
    
    if (access("/usr/bin/startplasma-x11", F_OK) == 0) {
        strcpy(sessions[count].name, "KDE Plasma");
        strcpy(sessions[count].exec, "startplasma-x11");
        count++;
    }
    else if (access("/usr/bin/startkde", F_OK) == 0) {
        strcpy(sessions[count].name, "KDE");
        strcpy(sessions[count].exec, "startkde");
        count++;
    }
    
    if (access("/usr/bin/startxfce4", F_OK) == 0) {
        strcpy(sessions[count].name, "XFCE");
        strcpy(sessions[count].exec, "startxfce4");
        count++;
    }
    
    if (access("/usr/bin/startlxde", F_OK) == 0) {
        strcpy(sessions[count].name, "LXDE");
        strcpy(sessions[count].exec, "startlxde");
        count++;
    }
    
    if (access("/usr/bin/startlxqt", F_OK) == 0) {
        strcpy(sessions[count].name, "LXQt");
        strcpy(sessions[count].exec, "startlxqt");
        count++;
    }
    
    if (access("/usr/bin/mate-session", F_OK) == 0) {
        strcpy(sessions[count].name, "MATE");
        strcpy(sessions[count].exec, "mate-session");
        count++;
    }
    
    if (access("/usr/bin/cinnamon-session", F_OK) == 0) {
        strcpy(sessions[count].name, "Cinnamon");
        strcpy(sessions[count].exec, "cinnamon-session");
        count++;
    }
    
    if (access("/usr/bin/enlightenment_start", F_OK) == 0) {
        strcpy(sessions[count].name, "Enlightenment");
        strcpy(sessions[count].exec, "enlightenment_start");
        count++;
    }
    
    if (access("/usr/bin/i3", F_OK) == 0) {
        strcpy(sessions[count].name, "i3");
        strcpy(sessions[count].exec, "i3");
        count++;
    }
    
    if (access("/usr/bin/sway", F_OK) == 0) {
        strcpy(sessions[count].name, "Sway");
        strcpy(sessions[count].exec, "sway");
        count++;
    }
    
    if (access("/usr/bin/openbox-session", F_OK) == 0) {
        strcpy(sessions[count].name, "Openbox");
        strcpy(sessions[count].exec, "openbox-session");
        count++;
    }
    
    if (access("/usr/bin/awesome", F_OK) == 0) {
        strcpy(sessions[count].name, "Awesome");
        strcpy(sessions[count].exec, "awesome");
        count++;
    }
    
    if (count == 0 && access("/usr/bin/xterm", F_OK) == 0) {
        strcpy(sessions[count].name, "XTerm");
        strcpy(sessions[count].exec, "xterm");
        count++;
    }
    
    return count;
}

int authenticate(const char *username, const char *password) {
    pam_handle_t *pamh = NULL;
    int retval;
    struct pam_conv conv = {
        .conv = conversation,
        .appdata_ptr = (void*)password
    };
    
    retval = pam_start("login", username, &conv, &pamh);
    if (retval != PAM_SUCCESS) {
        return 0;
    }
    
    retval = pam_authenticate(pamh, 0);
    if (retval != PAM_SUCCESS) {
        pam_end(pamh, retval);
        return 0;
    }
    
    retval = pam_acct_mgmt(pamh, 0);
    if (retval != PAM_SUCCESS) {
        pam_end(pamh, retval);
        return 0;
    }
    
    pam_end(pamh, PAM_SUCCESS);
    return 1;
}

pid_t start_dbus_session() {
    char dbus_dir[256];
    snprintf(dbus_dir, sizeof(dbus_dir), "/tmp/dbus-%d", getpid());
    mkdir(dbus_dir, 0700);
    
    pid_t pid = fork();
    if (pid == 0) {
        char *args[] = {
            "dbus-daemon",
            "--session",
            "--address=unix:path=/tmp/dbus-session",
            "--nofork",
            "--print-address=1",
            NULL
        };
        
        int fd = open("/tmp/dbus-address", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        dup2(fd, STDOUT_FILENO);
        close(fd);
        
        execvp("dbus-daemon", args);
        perror("Failed to start DBus");
        exit(1);
    }
    return pid;
}

int get_dbus_address(char *address, size_t size) {
    FILE *fp = fopen("/tmp/dbus-address", "r");
    if (!fp) {
        return 0;
    }
    
    if (fgets(address, size, fp)) {
        address[strcspn(address, "\n")] = '\0';
        fclose(fp);
        return 1;
    }
    
    fclose(fp);
    return 0;
}

void start_session(const char *username, const char *session_exec) {
    struct passwd *pwd = getpwnam(username);
    if (!pwd) {
        return;
    }
    
    // Устанавливаем базовые переменные окружения
    setenv("HOME", pwd->pw_dir, 1);
    setenv("SHELL", pwd->pw_shell, 1);
    setenv("USER", pwd->pw_name, 1);
    setenv("LOGNAME", pwd->pw_name, 1);
    setenv("DISPLAY", ":0", 1);
    
    // DBus и systemd переменные
    char runtime_dir[256];
    snprintf(runtime_dir, sizeof(runtime_dir), "/run/user/%d", pwd->pw_uid);
    mkdir(runtime_dir, 0777);
    
    // Важные переменные для X11 и DBus
    setenv("XDG_RUNTIME_DIR", runtime_dir, 1);
    setenv("XDG_SESSION_TYPE", "x11", 1);
    setenv("XDG_CURRENT_DESKTOP", "i3", 1);
    setenv("XDG_SESSION_CLASS", "user", 1);
    setenv("XDG_SESSION_DESKTOP", "i3", 1);
    
    // DBus адрес
    char dbus_addr[256];
    snprintf(dbus_addr, sizeof(dbus_addr), "unix:path=%s/bus", runtime_dir);
    setenv("DBUS_SESSION_BUS_ADDRESS", dbus_addr, 1);
    
    // PulseAudio
    char pulse_dir[256];
    snprintf(pulse_dir, sizeof(pulse_dir), "%s/pulse", runtime_dir);
    mkdir(pulse_dir, 0700);
    setenv("PULSE_RUNTIME_PATH", pulse_dir, 1);
    
    // Wayland переменные (на всякий случай)
    unsetenv("WAYLAND_DISPLAY");
    unsetenv("QT_QPA_PLATFORM");
    setenv("QT_QPA_PLATFORM", "xcb", 1);
    
    // Меняем группы и пользователя
    if (setgid(pwd->pw_gid) != 0) {
        perror("setgid failed");
        exit(1);
    }
    
    if (initgroups(pwd->pw_name, pwd->pw_gid) != 0) {
        perror("initgroups failed");
        exit(1);
    }
    
    if (setuid(pwd->pw_uid) != 0) {
        perror("setuid failed");
        exit(1);
    }
    
    // Запускаем сессию через login shell чтобы подгрузить все профили
    char *args[] = {
        pwd->pw_shell,
        "-l",
        "-c",
        "export DBUS_SESSION_BUS_ADDRESS && export PULSE_RUNTIME_PATH && export XDG_RUNTIME_DIR && exec $0",
        (char*)session_exec,
        NULL
    };
    
    execvp(pwd->pw_shell, args);
    perror("Failed to start session");
    exit(1);
}

pid_t start_x_server() {
    pid_t pid = fork();
    if (pid == 0) {
        setenv("DISPLAY", ":0", 1);
        
        char *args[] = {
            "X",
            ":0",
            "-ac",
            "-nolisten", "tcp",
            "-background", "none",
            "-noreset",
            "vt1",
            NULL
        };
        
        execvp("/usr/bin/X", args);
        perror("Failed to start X server");
        exit(1);
    }
    return pid;
}

int wait_for_x_server() {
    int attempts = 0;
    while (attempts < 50) {
        Display *test_display = XOpenDisplay(":0");
        if (test_display) {
            XCloseDisplay(test_display);
            return 1;
        }
        usleep(100000);
        attempts++;
    }
    return 0;
}

int wait_for_dbus() {
    int attempts = 0;
    while (attempts < 30) {
        if (access("/tmp/dbus-address", F_OK) == 0) {
            return 1;
        }
        usleep(100000);
        attempts++;
    }
    return 0;
}

void draw_gradient_background(DisplayManager *dm) {
    // Рисуем градиентный фон
    for (int y = 0; y < dm->height; y++) {
        double ratio = (double)y / dm->height;
        int r1 = (COLOR_BG1 >> 16) & 0xFF;
        int g1 = (COLOR_BG1 >> 8) & 0xFF;
        int b1 = COLOR_BG1 & 0xFF;
        int r2 = (COLOR_BG2 >> 16) & 0xFF;
        int g2 = (COLOR_BG2 >> 8) & 0xFF;
        int b2 = COLOR_BG2 & 0xFF;
        
        int r = r1 + (r2 - r1) * ratio;
        int g = g1 + (g2 - g1) * ratio;
        int b = b1 + (b2 - b1) * ratio;
        
        unsigned long color = (r << 16) | (g << 8) | b;
        XSetForeground(dm->display, dm->gc, color);
        XDrawLine(dm->display, dm->window, dm->gc, 0, y, dm->width, y);
    }
}

void draw_rounded_rect(DisplayManager *dm, int x, int y, int width, int height, int radius, unsigned long color) {
    XSetForeground(dm->display, dm->gc, color);
    
    // Основной прямоугольник
    XFillRectangle(dm->display, dm->window, dm->gc, x + radius, y, width - 2 * radius, height);
    XFillRectangle(dm->display, dm->window, dm->gc, x, y + radius, width, height - 2 * radius);
    
    // Углы
    XFillArc(dm->display, dm->window, dm->gc, x, y, 2 * radius, 2 * radius, 90 * 64, 90 * 64);
    XFillArc(dm->display, dm->window, dm->gc, x + width - 2 * radius, y, 2 * radius, 2 * radius, 0, 90 * 64);
    XFillArc(dm->display, dm->window, dm->gc, x, y + height - 2 * radius, 2 * radius, 2 * radius, 180 * 64, 90 * 64);
    XFillArc(dm->display, dm->window, dm->gc, x + width - 2 * radius, y + height - 2 * radius, 2 * radius, 2 * radius, 270 * 64, 90 * 64);
}

void draw_user_avatar(DisplayManager *dm, int x, int y, int selected) {
    int radius = AVATAR_SIZE / 2;
    
    // Фон аватарки
    XSetForeground(dm->display, dm->gc, selected ? COLOR_USER_SELECTED : COLOR_USER_BG);
    XFillArc(dm->display, dm->window, dm->gc, x, y, AVATAR_SIZE, AVATAR_SIZE, 0, 360 * 64);
    
    // Обводка если выбрано
    if (selected) {
        XSetForeground(dm->display, dm->gc, COLOR_HIGHLIGHT);
        XSetLineAttributes(dm->display, dm->gc, 3, LineSolid, CapRound, JoinRound);
        XDrawArc(dm->display, dm->window, dm->gc, x, y, AVATAR_SIZE, AVATAR_SIZE, 0, 360 * 64);
        XSetLineAttributes(dm->display, dm->gc, 1, LineSolid, CapRound, JoinRound);
    }
    
    // Смайлик
    XSetForeground(dm->display, dm->gc, COLOR_TEXT);
    
    // Глаза
    XFillArc(dm->display, dm->window, dm->gc, x + radius - 15, y + radius - 10, 10, 10, 0, 360 * 64);
    XFillArc(dm->display, dm->window, dm->gc, x + radius + 5, y + radius - 10, 10, 10, 0, 360 * 64);
    
    // Улыбка
    XDrawArc(dm->display, dm->window, dm->gc, x + radius - 15, y + radius, 30, 20, 0, 180 * 64);
}

void draw_mouse_cursor(DisplayManager *dm) {
    XSetForeground(dm->display, dm->gc, COLOR_HIGHLIGHT);
    XSetLineAttributes(dm->display, dm->gc, 2, LineSolid, CapRound, JoinRound);
    
    // Крестик без пересечения
    XDrawLine(dm->display, dm->window, dm->gc, dm->mouse_x - 8, dm->mouse_y, dm->mouse_x - 2, dm->mouse_y);
    XDrawLine(dm->display, dm->window, dm->gc, dm->mouse_x + 2, dm->mouse_y, dm->mouse_x + 8, dm->mouse_y);
    XDrawLine(dm->display, dm->window, dm->gc, dm->mouse_x, dm->mouse_y - 8, dm->mouse_x, dm->mouse_y - 2);
    XDrawLine(dm->display, dm->window, dm->gc, dm->mouse_x, dm->mouse_y + 2, dm->mouse_x, dm->mouse_y + 8);
    
    // Точка в центре
    XFillArc(dm->display, dm->window, dm->gc, dm->mouse_x - 2, dm->mouse_y - 2, 4, 4, 0, 360 * 64);
    
    XSetLineAttributes(dm->display, dm->gc, 1, LineSolid, CapRound, JoinRound);
}

int point_in_rect(int x, int y, int rect_x, int rect_y, int width, int height) {
    return (x >= rect_x && x <= rect_x + width && y >= rect_y && y <= rect_y + height);
}

void handle_mouse_click(DisplayManager *dm, int x, int y, int button) {
    // Клик по пользователям
    for (int i = 0; i < dm->user_count; i++) {
        int user_y = 120 + i * 140;
        int avatar_x = 80;
        int avatar_y = user_y;
        
        if (point_in_rect(x, y, avatar_x - 10, avatar_y - 10, AVATAR_SIZE + 20, AVATAR_SIZE + 20)) {
            for (int j = 0; j < dm->user_count; j++) {
                dm->users[j].selected = 0;
            }
            dm->users[i].selected = 1;
            dm->selected_user = i;
            dm->password_active = 1;
            dm->password_focus = 1;
            dm->show_sessions = 0;
            memset(dm->password, 0, sizeof(dm->password));
            return;
        }
    }
    
    if (dm->password_active && dm->selected_user >= 0) {
        // Клик по полю пароля
        int pass_field_x = dm->width/2 - 230;
        int pass_field_y = dm->height/2 - 30;
        
        if (point_in_rect(x, y, pass_field_x, pass_field_y, 460, 60)) {
            dm->password_focus = 1;
            return;
        }
        
        // Клик по кнопке сессии
        int session_button_x = dm->width/2 - 230;
        int session_button_y = dm->height/2 + 70;
        
        if (point_in_rect(x, y, session_button_x, session_button_y, 460, 50)) {
            dm->password_focus = 0;
            dm->show_sessions = !dm->show_sessions;
            return;
        }
        
        // Клик по элементам выпадающего списка сессий
        if (dm->show_sessions) {
            for (int i = 0; i < dm->session_count; i++) {
                int item_y = dm->height/2 + 130 + i * 50;
                if (point_in_rect(x, y, session_button_x, item_y, 460, 50)) {
                    dm->selected_session = i;
                    dm->show_sessions = 0;
                    return;
                }
            }
        }
        
        // Клик вне элементов - снимаем фокус
        dm->password_focus = 0;
    }
}

void show_error(DisplayManager *dm, const char *message) {
    strncpy(dm->error_message, message, sizeof(dm->error_message)-1);
    dm->error_time = time(NULL);
    dm->show_error = 1;
}

void show_warning(DisplayManager *dm, const char *message) {
    strncpy(dm->warning_message, message, sizeof(dm->warning_message)-1);
    dm->warning_time = time(NULL);
    dm->show_warning = 1;
}

void draw_notifications(DisplayManager *dm) {
    time_t current_time = time(NULL);
    
    if (dm->show_error && (current_time - dm->error_time < 5)) {
        draw_rounded_rect(dm, dm->width - 380, 30, 350, 70, 15, 0xff4444);
        
        XSetForeground(dm->display, dm->gc, 0xffffff);
        XDrawString(dm->display, dm->window, dm->gc, 
                   dm->width - 370, 55, "Error:", 6);
        XDrawString(dm->display, dm->window, dm->gc, 
                   dm->width - 370, 75, dm->error_message, strlen(dm->error_message));
    } else {
        dm->show_error = 0;
    }
    
    if (dm->show_warning && (current_time - dm->warning_time < 5)) {
        draw_rounded_rect(dm, dm->width - 380, 110, 350, 70, 15, 0xffcc00);
        
        XSetForeground(dm->display, dm->gc, 0x000000);
        XDrawString(dm->display, dm->window, dm->gc, 
                   dm->width - 370, 135, "Warning:", 8);
        XDrawString(dm->display, dm->window, dm->gc, 
                   dm->width - 370, 155, dm->warning_message, strlen(dm->warning_message));
    } else {
        dm->show_warning = 0;
    }
}

void handle_key_press(DisplayManager *dm, XKeyEvent *event) {
    if (dm->password_active && dm->selected_user >= 0 && dm->password_focus) {
        char keybuf[8];
        KeySym key;
        XLookupString(event, keybuf, sizeof(keybuf), &key, NULL);
        
        if (key == XK_Return) {
            if (authenticate(dm->users[dm->selected_user].username, dm->password)) {
                printf("Authentication successful! Starting session...\n");
                
                // Освобождаем ресурсы X11 перед запуском сессии
                XUngrabPointer(dm->display, CurrentTime);
                XUngrabKeyboard(dm->display, CurrentTime);
                
                if (dm->font) {
                    XFreeFont(dm->display, dm->font);
                }
                XFreeGC(dm->display, dm->gc);
                XDestroyWindow(dm->display, dm->window);
                XCloseDisplay(dm->display);
                
                // Запускаем сессию
                start_session(dm->users[dm->selected_user].username, 
                             dm->sessions[dm->selected_session].exec);
                
                // Если сессия завершилась, выходим
                exit(0);
            } else {
                printf("Authentication failed!\n");
                show_error(dm, "Invalid password");
                memset(dm->password, 0, sizeof(dm->password));
            }
        } else if (key == XK_BackSpace) {
            if (strlen(dm->password) > 0) {
                dm->password[strlen(dm->password)-1] = '\0';
            }
        } else if (keybuf[0] >= 32 && keybuf[0] <= 126) {
            if (strlen(dm->password) < sizeof(dm->password)-1) {
                strncat(dm->password, keybuf, 1);
            }
        }
    }
}

void draw_interface(DisplayManager *dm) {
    // Рисуем градиентный фон
    draw_gradient_background(dm);
    
    // Рисуем список пользователей слева
    for (int i = 0; i < dm->user_count; i++) {
        int y = 120 + i * 140;
        
        // Фон пользователя
        draw_rounded_rect(dm, 50, y - 15, 300, 110, 20, 
                         dm->users[i].selected ? COLOR_USER_SELECTED : COLOR_USER_BG);
        
        // Аватарка
        draw_user_avatar(dm, 80, y, dm->users[i].selected);
        
        // Имя пользователя
        XSetForeground(dm->display, dm->gc, COLOR_TEXT);
        XDrawString(dm->display, dm->window, dm->gc, 
                   150, y + AVATAR_SIZE/2 + 5, 
                   dm->users[i].display_name, strlen(dm->users[i].display_name));
    }
    
    // Поле ввода пароля
    if (dm->password_active && dm->selected_user >= 0) {
        // Основное поле
        draw_rounded_rect(dm, dm->width/2 - 250, dm->height/2 - 60, 500, 240, 30, COLOR_PASS_BG);
        
        // Заголовок
        XSetForeground(dm->display, dm->gc, COLOR_TEXT);
        XDrawString(dm->display, dm->window, dm->gc, 
                   dm->width/2 - 230, dm->height/2 - 85, 
                   "Enter Password:", 15);
        
        // Поле ввода (подсвечиваем если в фокусе)
        unsigned long pass_color = dm->password_focus ? COLOR_PASS_FOCUS : 0xffffff;
        draw_rounded_rect(dm, dm->width/2 - 230, dm->height/2 - 30, 460, 60, 20, pass_color);
        
        // Текст пароля
        XSetForeground(dm->display, dm->gc, 0x000000);
        if (strlen(dm->password) > 0) {
            char stars[strlen(dm->password) + 1];
            for (int i = 0; i < strlen(dm->password); i++) {
                stars[i] = '*';
            }
            stars[strlen(dm->password)] = '\0';
            
            // Центрируем текст пароля
            int text_width = XTextWidth(dm->font, stars, strlen(stars));
            int x_pos = dm->width/2 - text_width/2;
            XDrawString(dm->display, dm->window, dm->gc, 
                       x_pos, dm->height/2 + 10, stars, strlen(stars));
        } else if (dm->password_focus) {
            // Мигающий курсор когда поле в фокусе и пустое
            time_t current_time = time(NULL);
            if (current_time % 2 == 0) {
                XDrawString(dm->display, dm->window, dm->gc, 
                           dm->width/2 - 220, dm->height/2 + 10, "|", 1);
            }
        }
        
        // Кнопка выбора сессии
        draw_rounded_rect(dm, dm->width/2 - 230, dm->height/2 + 70, 460, 50, 20, COLOR_ACCENT1);
        
        XSetForeground(dm->display, dm->gc, COLOR_TEXT);
        char session_text[64];
        if (dm->session_count > 0) {
            snprintf(session_text, sizeof(session_text), "Session: %s ▼", 
                    dm->sessions[dm->selected_session].name);
        } else {
            strcpy(session_text, "No sessions available");
        }
        
        // Центрируем текст сессии
        int text_width = XTextWidth(dm->font, session_text, strlen(session_text));
        int x_pos = dm->width/2 - text_width/2;
        XDrawString(dm->display, dm->window, dm->gc, 
                   x_pos, dm->height/2 + 100, session_text, strlen(session_text));
        
        // Выпадающий список сессий
        if (dm->show_sessions) {
            draw_rounded_rect(dm, dm->width/2 - 230, dm->height/2 + 130, 460, dm->session_count * 50, 20, 0xffffff);
            
            for (int i = 0; i < dm->session_count; i++) {
                if (i == dm->selected_session) {
                    draw_rounded_rect(dm, dm->width/2 - 230, dm->height/2 + 130 + i * 50, 460, 50, 20, COLOR_HIGHLIGHT);
                }
                
                XSetForeground(dm->display, dm->gc, i == dm->selected_session ? 0xffffff : 0x000000);
                
                // Центрируем текст сессии
                text_width = XTextWidth(dm->font, dm->sessions[i].name, strlen(dm->sessions[i].name));
                x_pos = dm->width/2 - text_width/2;
                XDrawString(dm->display, dm->window, dm->gc, 
                           x_pos, dm->height/2 + 160 + i * 50, 
                           dm->sessions[i].name, strlen(dm->sessions[i].name));
            }
        }
    }
    
    // Уведомления
    draw_notifications(dm);
    
    // Курсор мыши
    draw_mouse_cursor(dm);
    
    XFlush(dm->display);
}

void signal_handler(int sig) {
    exit(0);
}

int main() {
    DisplayManager dm;
    memset(&dm, 0, sizeof(DisplayManager));
    
    dm.mouse_x = 100;
    dm.mouse_y = 100;
    dm.mouse_buttons = 0;
    dm.show_error = 0;
    dm.show_warning = 0;
    dm.error_time = 0;
    dm.warning_time = 0;
    dm.password_focus = 0;
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("Starting DBus session bus...\n");
    
    dm.dbus_pid = start_dbus_session();
    if (dm.dbus_pid < 0) {
        fprintf(stderr, "Failed to start DBus\n");
        return 1;
    }
    
    printf("Waiting for DBus to start...\n");
    if (!wait_for_dbus()) {
        fprintf(stderr, "DBus failed to start\n");
        kill(dm.dbus_pid, SIGTERM);
        return 1;
    }
    
    if (get_dbus_address(dm.dbus_address, sizeof(dm.dbus_address))) {
        printf("DBus address: %s\n", dm.dbus_address);
        setenv("DBUS_SESSION_BUS_ADDRESS", dm.dbus_address, 1);
    }
    
    printf("Starting X server...\n");
    dm.xserver_pid = start_x_server();
    if (dm.xserver_pid < 0) {
        fprintf(stderr, "Failed to start X server\n");
        kill(dm.dbus_pid, SIGTERM);
        return 1;
    }
    
    printf("Waiting for X server to start...\n");
    if (!wait_for_x_server()) {
        fprintf(stderr, "X server failed to start\n");
        kill(dm.xserver_pid, SIGTERM);
        kill(dm.dbus_pid, SIGTERM);
        return 1;
    }
    
    printf("X server started successfully\n");
    setenv("DISPLAY", ":0", 1);
    
    dm.display = XOpenDisplay(":0");
    if (!dm.display) {
        fprintf(stderr, "Cannot open X display: :0\n");
        kill(dm.xserver_pid, SIGTERM);
        kill(dm.dbus_pid, SIGTERM);
        return 1;
    }
    
    dm.screen = DefaultScreen(dm.display);
    dm.width = DisplayWidth(dm.display, dm.screen);
    dm.height = DisplayHeight(dm.display, dm.screen);
    
    dm.window = XCreateSimpleWindow(dm.display, RootWindow(dm.display, dm.screen),
                                   0, 0, dm.width, dm.height, 0,
                                   BlackPixel(dm.display, dm.screen),
                                   BlackPixel(dm.display, dm.screen));
    
    XStoreName(dm.display, dm.window, "Modern Display Manager");
     // Устанавливаем события
    XSelectInput(dm.display, dm.window,
                ExposureMask | KeyPressMask | ButtonPressMask |
                ButtonReleaseMask | PointerMotionMask | StructureNotifyMask);

    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    XChangeWindowAttributes(dm.display, dm.window, CWOverrideRedirect, &attrs);

    XMapWindow(dm.display, dm.window);
    XRaiseWindow(dm.display, dm.window);

    XGrabPointer(dm.display, dm.window, True,
                ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                GrabModeAsync, GrabModeAsync, dm.window, None, CurrentTime);
    XGrabKeyboard(dm.display, dm.window, True, GrabModeAsync, GrabModeAsync, CurrentTime);

    dm.gc = XCreateGC(dm.display, dm.window, 0, NULL);
    XSetForeground(dm.display, dm.gc, WhitePixel(dm.display, dm.screen));
    XSetBackground(dm.display, dm.gc, BlackPixel(dm.display, dm.screen));

    // Загружаем большой шрифт DejaVu Sans Mono 18
    dm.font = XLoadQueryFont(dm.display, "-misc-dejavu sans mono-medium-r-normal--18-0-0-0-m-0-iso10646-1");
    if (!dm.font) {
        dm.font = XLoadQueryFont(dm.display, "9x15");
    }
    if (dm.font) {
        XSetFont(dm.display, dm.gc, dm.font->fid);
    }

    // Получаем данные
    dm.user_count = get_users(dm.users);
    dm.selected_user = 0;
    dm.password_active = 0;

    dm.session_count = get_sessions(dm.sessions);
    dm.selected_session = 0;
    dm.show_sessions = 0;

    XEvent event;
    int running = 1;

    // Устанавливаем высокую частоту обновления
    struct timespec frame_time;
    frame_time.tv_sec = 0;
    frame_time.tv_nsec = 1000000000 / FPS;

    // Двойная буферизация для избежания мерцания
    Pixmap buffer = XCreatePixmap(dm.display, dm.window, dm.width, dm.height, DefaultDepth(dm.display, dm.screen));
    GC buffer_gc = XCreateGC(dm.display, buffer, 0, NULL);

    while (running) {
        // Обрабатываем все события
        while (XPending(dm.display)) {
            XNextEvent(dm.display, &event);

            switch (event.type) {
                case MotionNotify:
                    dm.mouse_x = event.xmotion.x;
                    dm.mouse_y = event.xmotion.y;
                    break;

                case ButtonPress:
                    dm.mouse_x = event.xbutton.x;
                    dm.mouse_y = event.xbutton.y;
                    dm.mouse_buttons |= (1 << (event.xbutton.button - 1));
                    handle_mouse_click(&dm, event.xbutton.x, event.xbutton.y, event.xbutton.button);
                    break;

                case ButtonRelease:
                    dm.mouse_x = event.xbutton.x;
                    dm.mouse_y = event.xbutton.y;
                    dm.mouse_buttons &= ~(1 << (event.xbutton.button - 1));
                    break;

                case KeyPress:
                    handle_key_press(&dm, &event.xkey);
                    break;

                case ConfigureNotify:
                    dm.width = event.xconfigure.width;
                    dm.height = event.xconfigure.height;
                    XFreePixmap(dm.display, buffer);
                    buffer = XCreatePixmap(dm.display, dm.window, dm.width, dm.height, DefaultDepth(dm.display, dm.screen));
                    break;
            }
        }

        // Отрисовываем в буфер
        DisplayManager dm_buffer = dm;
        dm_buffer.display = dm.display;
        dm_buffer.window = buffer;
        dm_buffer.gc = buffer_gc;

        draw_interface(&dm_buffer);

        // Копируем буфер на экран
        XCopyArea(dm.display, buffer, dm.window, dm.gc, 0, 0, dm.width, dm.height, 0, 0);

        // Задержка для поддержания FPS
        nanosleep(&frame_time, NULL);
    }

    // Cleanup
    XFreePixmap(dm.display, buffer);
    XFreeGC(dm.display, buffer_gc);

    if (dm.font) {
        XFreeFont(dm.display, dm.font);
    }
    XFreeGC(dm.display, dm.gc);
    XDestroyWindow(dm.display, dm.window);
    XCloseDisplay(dm.display);

    kill(dm.xserver_pid, SIGTERM);
    kill(dm.dbus_pid, SIGTERM);

    unlink("/tmp/dbus-address");

    return 0;
}
