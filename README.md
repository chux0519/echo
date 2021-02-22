Echo server example using libevent.

Both UDP and TCP are supported.

## build

> gcc ./echo.c -o echo -levent -g

## usage

> echo -p 8080

then use netcat(openbsd version) to test it

for tcp

> nc localhost 8080

for udp

> nc -u localhost 8080
