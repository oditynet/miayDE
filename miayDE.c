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

#include <systemd/sd-login.h>

#define MAX_USERS 20
#define AVATAR_SIZE 60
#define SESSION_NAME_MAX 32

// Добавляем в структуру DisplayManager
pid_t systemd_pid;
pid_t pulseaudio_pid;

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
} DisplayManager;


// Функция для запуска systemd user session
pid_t start_systemd_user_session() {
    pid_t pid = fork();
    if (pid == 0) {
        // Запускаем systemd user instance
        char *args[] = {
            "systemd",
            "--user",
            "--unit=basic.target",
            NULL
        };
        
        setenv("XDG_RUNTIME_DIR", "/run/user/1000", 1);
        setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/run/user/1000/bus", 1);
        
        execvp("/usr/lib/systemd/systemd", args);
        perror("Failed to start systemd user session");
        exit(1);
    }
    return pid;
}

// Функция для запуска PulseAudio
pid_t start_pulseaudio() {
    pid_t pid = fork();
    if (pid == 0) {
        char *args[] = {
            "pulseaudio",
            "--daemonize=no",
            "--exit-idle-time=-1",
            NULL
        };
        
        setenv("PULSE_RUNTIME_PATH", "/run/user/1000/pulse", 1);
        execvp("pulseaudio", args);
        perror("Failed to start PulseAudio");
        exit(1);
    }
    return pid;
}

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
                // Ignore informative messages
                break;
            default:
                free(response);
                return PAM_CONV_ERR;
        }
    }
    
    *resp = response;
    return PAM_SUCCESS;
}

// Функция для получения списка пользователей
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
    
    // GNOME
    if (access("/usr/bin/gnome-session", F_OK) == 0) {
        strcpy(sessions[count].name, "GNOME");
        strcpy(sessions[count].exec, "gnome-session");
        count++;
    }
    
    // KDE Plasma
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
    
    // XFCE
    if (access("/usr/bin/startxfce4", F_OK) == 0) {
        strcpy(sessions[count].name, "XFCE");
        strcpy(sessions[count].exec, "startxfce4");
        count++;
    }
    
    // LXDE
    if (access("/usr/bin/startlxde", F_OK) == 0) {
        strcpy(sessions[count].name, "LXDE");
        strcpy(sessions[count].exec, "startlxde");
        count++;
    }
    
    // LXQt
    if (access("/usr/bin/startlxqt", F_OK) == 0) {
        strcpy(sessions[count].name, "LXQt");
        strcpy(sessions[count].exec, "startlxqt");
        count++;
    }
    
    // MATE
    if (access("/usr/bin/mate-session", F_OK) == 0) {
        strcpy(sessions[count].name, "MATE");
        strcpy(sessions[count].exec, "mate-session");
        count++;
    }
    
    // Cinnamon
    if (access("/usr/bin/cinnamon-session", F_OK) == 0) {
        strcpy(sessions[count].name, "Cinnamon");
        strcpy(sessions[count].exec, "cinnamon-session");
        count++;
    }
    
    // Enlightenment
    if (access("/usr/bin/enlightenment_start", F_OK) == 0) {
        strcpy(sessions[count].name, "Enlightenment");
        strcpy(sessions[count].exec, "enlightenment_start");
        count++;
    }
    
    // i3
    if (access("/usr/bin/i3", F_OK) == 0) {
        strcpy(sessions[count].name, "i3");
        strcpy(sessions[count].exec, "i3");
        count++;
    }
    
    // Sway (Wayland)
    if (access("/usr/bin/sway", F_OK) == 0) {
        strcpy(sessions[count].name, "Sway");
        strcpy(sessions[count].exec, "sway");
        count++;
    }
    
    // Openbox
    if (access("/usr/bin/openbox-session", F_OK) == 0) {
        strcpy(sessions[count].name, "Openbox");
        strcpy(sessions[count].exec, "openbox-session");
        count++;
    }
    
    // Awesome
    if (access("/usr/bin/awesome", F_OK) == 0) {
        strcpy(sessions[count].name, "Awesome");
        strcpy(sessions[count].exec, "awesome");
        count++;
    }
    
    // Fallback to xterm
    if (count == 0 && access("/usr/bin/xterm", F_OK) == 0) {
        strcpy(sessions[count].name, "XTerm");
        strcpy(sessions[count].exec, "xterm");
        count++;
    }
    
    return count;
}
// Функция для получения списка сессий
/*int get_sessions(Session *sessions) {
    int count = 0;
    
    // i3
    if (access("/usr/bin/i3", F_OK) == 0) {
        strcpy(sessions[count].name, "i3");
        strcpy(sessions[count].exec, "i3");
        count++;
    }
    if (access("/usr/bin/sway", F_OK) == 0) {
        strcpy(sessions[count].name, "sway");
        strcpy(sessions[count].exec, "sway");
        count++;
    }
    
    // Fallback to xterm
    if (count == 0 && access("/usr/bin/xterm", F_OK) == 0) {
        strcpy(sessions[count].name, "XTerm");
        strcpy(sessions[count].exec, "xterm");
        count++;
    }
    
    return count;
}*/

// Функция для аутентификации PAM
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

// Функция для запуска DBus session bus
pid_t start_dbus_session() {
    char dbus_dir[256];
    snprintf(dbus_dir, sizeof(dbus_dir), "/tmp/dbus-%d", getpid());
    mkdir(dbus_dir, 0700);
    
    pid_t pid = fork();
    if (pid == 0) {
        // Дочерний процесс - запускаем DBus session bus
        char *args[] = {
            "dbus-daemon",
            "--session",
            "--address=unix:path=/tmp/dbus-session",
            "--nofork",
            "--print-address=1",
            NULL
        };
        
        // Перенаправляем вывод для получения адреса
        int fd = open("/tmp/dbus-address", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        dup2(fd, STDOUT_FILENO);
        close(fd);
        
        execvp("dbus-daemon", args);
        perror("Failed to start DBus");
        exit(1);
    }
    return pid;
}

// Функция для получения DBus адреса
int get_dbus_address(char *address, size_t size) {
    FILE *fp = fopen("/tmp/dbus-address", "r");
    if (!fp) {
        return 0;
    }
    
    if (fgets(address, size, fp)) {
        // Убираем перевод строки
        address[strcspn(address, "\n")] = '\0';
        fclose(fp);
        return 1;
    }
    
    fclose(fp);
    return 0;
}

// Функция для запуска сессии с правильными переменными окружения
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
    mkdir(runtime_dir, 0700);
    
    // Важные переменные для X11 и DBus
    setenv("XDG_RUNTIME_DIR", "/run/user/1000", 1);
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
    
    
    // DBus переменные
/*    char dbus_address[256];
    if (get_dbus_address(dbus_address, sizeof(dbus_address))) {
        setenv("DBUS_SESSION_BUS_ADDRESS", dbus_address, 1);
    } else {
        // Fallback адрес
        setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/tmp/dbus-session", 1);
    }
    
    // PulseAudio переменные
    setenv("PULSE_RUNTIME_PATH", "/run/user/1000/pulse", 1);
    setenv("PULSE_COOKIE", "/home/user/.config/pulse/cookie", 0);
*/    
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
    

    // Создаем runtime directory если не существует
//    char runtime_dir[256];
    snprintf(runtime_dir, sizeof(runtime_dir), "/run/user/%d", pwd->pw_uid);
    mkdir(runtime_dir, 0700);

//setenv("PULSE_RUNTIME_PATH", "/run/user/1000/pulse", 1);
setenv("PULSE_CONFIG_PATH", "/home/user/.config/pulse", 0);

// Создаем pulse runtime directory
//char pulse_dir[256];
snprintf(pulse_dir, sizeof(pulse_dir), "/run/user/%d/pulse", pwd->pw_uid);
mkdir(pulse_dir, 0700);
    
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
    exit(1);
}

// Функция для запуска X сервера
pid_t start_x_server() {
    pid_t pid = fork();
    if (pid == 0) {
        // Дочерний процесс - запускаем X сервер
        setenv("DISPLAY", ":0", 1);
        
        // Аргументы для X сервера
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

// Функция для проверки готовности X сервера
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

// Функция для проверки готовности DBus
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

// Функция для рисования круга (аватарки)
void draw_circle(Display *display, Window window, GC gc, int x, int y, int radius, int filled) {
    if (filled) {
        XFillArc(display, window, gc, x - radius, y - radius, 
                 radius * 2, radius * 2, 0, 360 * 64);
    } else {
        XDrawArc(display, window, gc, x - radius, y - radius, 
                 radius * 2, radius * 2, 0, 360 * 64);
    }
}

// Функция для проверки попадания точки в прямоугольник
int point_in_rect(int x, int y, int rect_x, int rect_y, int width, int height) {
    return (x >= rect_x && x <= rect_x + width && y >= rect_y && y <= rect_y + height);
}

// Функция для рисования курсора мыши
void draw_mouse_cursor(DisplayManager *dm) {
    XSetForeground(dm->display, dm->gc, 0xffffff);
    XDrawLine(dm->display, dm->window, dm->gc, dm->mouse_x, dm->mouse_y, dm->mouse_x + 10, dm->mouse_y);
    XDrawLine(dm->display, dm->window, dm->gc, dm->mouse_x, dm->mouse_y, dm->mouse_x, dm->mouse_y + 10);
    XDrawLine(dm->display, dm->window, dm->gc, dm->mouse_x, dm->mouse_y, dm->mouse_x + 7, dm->mouse_y + 7);
}

// Функция для обработки кликов мыши
void handle_mouse_click(DisplayManager *dm, int x, int y, int button) {
    for (int i = 0; i < dm->user_count; i++) {
        int user_y = 80 + i * 90;
        int avatar_x = 50;
        int avatar_y = user_y;
        
        if (point_in_rect(x, y, avatar_x, avatar_y, AVATAR_SIZE + 200, AVATAR_SIZE)) {
            for (int j = 0; j < dm->user_count; j++) {
                dm->users[j].selected = 0;
            }
            dm->users[i].selected = 1;
            dm->selected_user = i;
            dm->password_active = 1;
            dm->show_sessions = 0;
            memset(dm->password, 0, sizeof(dm->password));
            return;
        }
    }
    
    if (dm->password_active && dm->selected_user >= 0) {
        int session_button_x = dm->width/2 - 175;
        int session_button_y = dm->height/2 + 40;
        
        if (point_in_rect(x, y, session_button_x, session_button_y, 350, 30)) {
            dm->show_sessions = !dm->show_sessions;
            return;
        }
        
        if (dm->show_sessions) {
            for (int i = 0; i < dm->session_count; i++) {
                int item_y = dm->height/2 + 70 + i * 30;
                if (point_in_rect(x, y, session_button_x, item_y, 350, 30)) {
                    dm->selected_session = i;
                    dm->show_sessions = 0;
                    return;
                }
            }
        }
    }
}

// Функция для рисования интерфейса
void draw_interface(DisplayManager *dm) {
    XClearWindow(dm->display, dm->window);
    
    XSetForeground(dm->display, dm->gc, 0x2d2d2d);
    XFillRectangle(dm->display, dm->window, dm->gc, 0, 0, dm->width, dm->height);
    
    for (int i = 0; i < dm->user_count; i++) {
        int y = 80 + i * 90;
        
        XSetForeground(dm->display, dm->gc, 0x888888);
        draw_circle(dm->display, dm->window, dm->gc, 50 + AVATAR_SIZE/2, y + AVATAR_SIZE/2, AVATAR_SIZE/2, 1);
        
        XSetForeground(dm->display, dm->gc, 0x000000);
        draw_circle(dm->display, dm->window, dm->gc, 50 + AVATAR_SIZE/3, y + AVATAR_SIZE/3, AVATAR_SIZE/10, 1);
        draw_circle(dm->display, dm->window, dm->gc, 50 + 2*AVATAR_SIZE/3, y + AVATAR_SIZE/3, AVATAR_SIZE/10, 1);
        
        XDrawArc(dm->display, dm->window, dm->gc, 
                50 + AVATAR_SIZE/4, y + AVATAR_SIZE/2, 
                AVATAR_SIZE/2, AVATAR_SIZE/3, 
                0, 180 * 64);
        
        if (dm->users[i].selected) {
            XSetForeground(dm->display, dm->gc, 0x3498db);
            draw_circle(dm->display, dm->window, dm->gc, 50 + AVATAR_SIZE/2, y + AVATAR_SIZE/2, AVATAR_SIZE/2 + 5, 0);
        }
        
        XSetForeground(dm->display, dm->gc, 0xffffff);
        XDrawString(dm->display, dm->window, dm->gc, 
                   50 + AVATAR_SIZE + 15, y + AVATAR_SIZE/2 + 5, 
                   dm->users[i].display_name, strlen(dm->users[i].display_name));
    }
    
    if (dm->password_active && dm->selected_user >= 0) {
        XSetForeground(dm->display, dm->gc, 0x3498db);
        XFillRectangle(dm->display, dm->window, dm->gc, 
                      dm->width/2 - 175, dm->height/2 - 30, 350, 60);
        
        XSetForeground(dm->display, dm->gc, 0xffffff);
        XFillRectangle(dm->display, dm->window, dm->gc, 
                      dm->width/2 - 170, dm->height/2 - 25, 340, 50);
        
        XSetForeground(dm->display, dm->gc, 0x333333);
        XDrawString(dm->display, dm->window, dm->gc, 
                   dm->width/2 - 165, dm->height/2 - 40, 
                   "Password:", 9);
        
        XSetForeground(dm->display, dm->gc, 0x000000);
        if (strlen(dm->password) > 0) {
            char stars[strlen(dm->password) + 1];
            for (int i = 0; i < strlen(dm->password); i++) {
                stars[i] = '*';
            }
            stars[strlen(dm->password)] = '\0';
            XDrawString(dm->display, dm->window, dm->gc, 
                       dm->width/2 - 160, dm->height/2 + 5, stars, strlen(stars));
        }
        
        XSetForeground(dm->display, dm->gc, 0x555555);
        XFillRectangle(dm->display, dm->window, dm->gc, 
                      dm->width/2 - 175, dm->height/2 + 40, 350, 30);
        
        XSetForeground(dm->display, dm->gc, 0xffffff);
        char session_text[64];
        if (dm->session_count > 0) {
            snprintf(session_text, sizeof(session_text), "Session: %s ▼", 
                    dm->sessions[dm->selected_session].name);
        } else {
            strcpy(session_text, "No sessions available");
        }
        XDrawString(dm->display, dm->window, dm->gc, 
                   dm->width/2 - 160, dm->height/2 + 60, session_text, strlen(session_text));
        
        if (dm->show_sessions) {
            XSetForeground(dm->display, dm->gc, 0xffffff);
            XFillRectangle(dm->display, dm->window, dm->gc, 
                          dm->width/2 - 175, dm->height/2 + 70, 350, dm->session_count * 30);
            
            for (int i = 0; i < dm->session_count; i++) {
                if (i == dm->selected_session) {
                    XSetForeground(dm->display, dm->gc, 0x3498db);
                    XFillRectangle(dm->display, dm->window, dm->gc, 
                                  dm->width/2 - 175, dm->height/2 + 70 + i * 30, 350, 30);
                }
                
                XSetForeground(dm->display, dm->gc, i == dm->selected_session ? 0xffffff : 0x333333);
                XDrawString(dm->display, dm->window, dm->gc, 
                           dm->width/2 - 160, dm->height/2 + 90 + i * 30, 
                           dm->sessions[i].name, strlen(dm->sessions[i].name));
            }
        }
    }
    
    draw_mouse_cursor(dm);
    XFlush(dm->display);
}

// Обработчик сигналов для cleanup
void signal_handler(int sig) {
    exit(0);
}

int main() {
    DisplayManager dm;
    memset(&dm, 0, sizeof(DisplayManager));
    
    dm.mouse_x = 100;
    dm.mouse_y = 100;
    dm.mouse_buttons = 0;
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("Starting DBus session bus...\n");
    
    // Запускаем DBus session bus
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
    
    // Получаем DBus адрес
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
    
    XStoreName(dm.display, dm.window, "Simple Display Manager");
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
    
    dm.font = XLoadQueryFont(dm.display, "fixed");
    if (dm.font) {
        XSetFont(dm.display, dm.gc, dm.font->fid);
    }
    
    dm.user_count = get_users(dm.users);
    dm.selected_user = 0;
    dm.password_active = 0;
    
    dm.session_count = get_sessions(dm.sessions);
    dm.selected_session = 0;
    dm.show_sessions = 0;
    
    XEvent event;
    int running = 1;
    
    while (running) {
        XNextEvent(dm.display, &event);
        
        switch (event.type) {
            case Expose:
                draw_interface(&dm);
                break;
                
            case MotionNotify:
                dm.mouse_x = event.xmotion.x;
                dm.mouse_y = event.xmotion.y;
                draw_interface(&dm);
                break;
                
            case ButtonPress:
                dm.mouse_x = event.xbutton.x;
                dm.mouse_y = event.xbutton.y;
                dm.mouse_buttons |= (1 << (event.xbutton.button - 1));
                handle_mouse_click(&dm, event.xbutton.x, event.xbutton.y, event.xbutton.button);
                draw_interface(&dm);
                break;
                
            case ButtonRelease:
                dm.mouse_x = event.xbutton.x;
                dm.mouse_y = event.xbutton.y;
                dm.mouse_buttons &= ~(1 << (event.xbutton.button - 1));
                draw_interface(&dm);
                break;
                
            case KeyPress:
                if (dm.password_active && dm.selected_user >= 0) {
                    char keybuf[8];
                    KeySym key;
                    XLookupString(&event.xkey, keybuf, sizeof(keybuf), &key, NULL);
                    
                    if (key == XK_Return) {
                        if (authenticate(dm.users[dm.selected_user].username, dm.password)) {
                            printf("Authentication successful!\n");
                            XUngrabPointer(dm.display, CurrentTime);
                            XUngrabKeyboard(dm.display, CurrentTime);
                            start_session(dm.users[dm.selected_user].username, 
                                         dm.sessions[dm.selected_session].exec);
                            running = 0;
                        } else {
                            printf("Authentication failed!\n");
                            memset(dm.password, 0, sizeof(dm.password));
                        }
                        draw_interface(&dm);
                    } else if (key == XK_BackSpace) {
                        if (strlen(dm.password) > 0) {
                            dm.password[strlen(dm.password)-1] = '\0';
                            draw_interface(&dm);
                        }
                    } else if (keybuf[0] >= 32 && keybuf[0] <= 126) {
                        if (strlen(dm.password) < sizeof(dm.password)-1) {
                            strncat(dm.password, keybuf, 1);
                            draw_interface(&dm);
                        }
                    }
                }
                break;
                
            case ConfigureNotify:
                dm.width = event.xconfigure.width;
                dm.height = event.xconfigure.height;
                draw_interface(&dm);
                break;
        }
    }
    
    if (dm.font) {
        XFreeFont(dm.display, dm.font);
    }
    XFreeGC(dm.display, dm.gc);
    XDestroyWindow(dm.display, dm.window);
    XCloseDisplay(dm.display);
    
    kill(dm.xserver_pid, SIGTERM);
    kill(dm.dbus_pid, SIGTERM);
    
    // Удаляем временные файлы
    unlink("/tmp/dbus-address");
    
    return 0;
}
