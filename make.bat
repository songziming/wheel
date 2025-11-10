:: 需要使用 wsl 启动 bash 程序，这样 wsl 里的环境变量才能生效
:: -l 表示 login
:: -i 表示 interactive

wsl -e bash -li -c "make %*"
