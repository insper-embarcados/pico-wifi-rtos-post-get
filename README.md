# Wifi

Esse exemplo fica executando um `post` em um servidor `http` local (implementando via flask).

1. Conecte o seu computador em uma rede local
2. Execute o `python/main.py`
3. Agora modifique o `main/main.c`:

``` c
#define WIFI_SSID "your-wifi"
#define WIFI_PASSWORD "your-password"
#define SERVER_IP "your-ip"
#define SERVER_PORT 5000
```

Agora execute o código na rasp, abra o terminal para observar as mensagens de erro.
