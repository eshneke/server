# Установка
sudo dpkg -i simple-server-1.0.deb

# Перезагрузка systemd
sudo systemctl daemon-reload

# Запуск
sudo systemctl start simple-server

# Проверка статуса
sudo systemctl status simple-server

# Остановка
sudo systemctl stop simple-server