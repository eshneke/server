# Собрать бинарник
make

# Скопировать файлы
cp simple-server simple-server-1.0/usr/bin/
cp simple-server.service simple-server-1.0/usr/lib/systemd/system/

# Установить права
chmod 755 simple-server-1.0/usr/bin/simple-server
chmod 644 simple-server-1.0/usr/lib/systemd/system/simple-server.service

# Собрать .deb
dpkg-deb --build simple-server-1.0