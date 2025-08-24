<div align="center">
<img src="https://github.com/oditynet/miayDE/blob/main/logo.png" title="example" width="800" />
  <h1>  miayDE </h1>
</div>

<img alt="GitHub code size in bytes" src="https://img.shields.io/github/languages/code-size/oditynet/miayDE"></img>
<img alt="GitHub license" src="https://img.shields.io/github/license/oditynet/miayDE"></img>
<img alt="GitHub commit activity" src="https://img.shields.io/github/commit-activity/m/oditynet/miayDE"></img>
<img alt="GitHub Repo stars" src="https://img.shields.io/github/stars/oditynet/miayDE"></img>

*В разработке

# miayDE
miayDE for X11  - это дисплейный менеджер X Window System, который стремится быть лёгким, быстрым, расширяемым и поддерживающим множество рабочих столов. 
Авторизация проходит спомощью PAM, курсор и графика рисуются вручную.

Build:
```
gcc -o miayDE 11.c -lX11 -lpam -lm -ldbus-1 -I /usr/include/dbus-1.0 -I /usr/lib/dbus-1.0/include
cp miayDE /usr/bin
```
vim /etc/systemd/system/miayDE.service
```
[Unit]
Description=Miay Display Manager
Documentation=man:miay-dm(1)
After=systemd-user-sessions.service plymouth-quit.service systemd-graphic.target

[Service]
ExecStart=/usr/bin/miayDE
Restart=always
IgnoreSIGPIPE=no

[Install]
Alias=display-manager.service
WantedBy=systemd-graphic.target
```

Настройка автозапуска (если у вас lightdm)
```
systemctl daemon-reload
systemctl disable lightdm.service
systemctl enable miayDE.service
```


<img src="https://github.com/oditynet/miayDE/blob/main/screen.jpg" title="example" width="800" />

Problem:
1) Пока система инициализbрует DBus, но звука нет (не все окружение заполнено )
