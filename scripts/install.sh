#!/bin/bash
# scripts/install.sh - Полная установка Nexus Ledger

set -e

# Цвета
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

print_banner() {
    echo -e "${CYAN}"
    echo "╔═══════════════════════════════════════════════════════════════════╗"
    echo "║                                                                   ║"
    echo "║     ███╗   ██╗███████╗██╗  ██╗██╗   ██╗███████╗                   ║"
    echo "║     ████╗  ██║██╔════╝╚██╗██╔╝██║   ██║██╔════╝                   ║"
    echo "║     ██╔██╗ ██║█████╗   ╚███╔╝ ██║   ██║███████╗                   ║"
    echo "║     ██║╚██╗██║██╔══╝   ██╔██╗ ██║   ██║╚════██║                   ║"
    echo "║     ██║ ╚████║███████╗██╔╝ ██╗╚██████╔╝███████║                   ║"
    echo "║     ╚═╝  ╚═══╝╚══════╝╚═╝  ╚═╝ ╚═════╝ ╚══════╝                   ║"
    echo "║                                                                   ║"
    echo "║              ДЕЦЕНТРАЛИЗОВАННАЯ P2P СЕТЬ                          ║"
    echo "║                    С РАСПРЕДЕЛЁННЫМ РЕЕСТРОМ                      ║"
    echo "║                                                                   ║"
    echo "╚═══════════════════════════════════════════════════════════════════╝"
    echo -e "${NC}"
}

print_step() {
    echo -e "${GREEN}▶ $1${NC}"
}

print_success() {
    echo -e "${GREEN}✅ $1${NC}"
}

print_error() {
    echo -e "${RED}❌ $1${NC}"
}

print_info() {
    echo -e "${BLUE}ℹ️  $1${NC}"
}

check_os() {
    print_step "Проверка операционной системы..."
    if [[ "$(uname)" != "Linux" ]]; then
        print_error "Этот скрипт предназначен только для Linux"
        exit 1
    fi
    print_success "ОС: $(uname -a | cut -d' ' -f1-3)"
}

update_system() {
    print_step "Обновление системы..."
    sudo apt update -qq
    sudo apt upgrade -y -qq
    print_success "Система обновлена"
}

install_dependencies() {
    print_step "Установка зависимостей..."
    
    local packages=(
        build-essential
        cmake
        g++
        libboost-all-dev
        libssl-dev
        libsqlite3-dev
        nlohmann-json3-dev
        curl
        git
        python3
        docker.io
        docker-compose
    )
    
    for pkg in "${packages[@]}"; do
        echo -n "  Установка $pkg... "
        if sudo apt install -y $pkg > /dev/null 2>&1; then
            echo -e "${GREEN}OK${NC}"
        else
            echo -e "${RED}FAILED${NC}"
        fi
    done
    
    # Добавляем пользователя в группу docker
    sudo usermod -aG docker $USER
    print_success "Все зависимости установлены (Docker добавлен)"
}

clone_repo() {
    print_step "Клонирование репозитория..."
    
    local repo_url="https://github.com/Xeramen/nexus-ledger.git"
    local install_dir="/opt/nexus-ledger"
    
    if [[ -d "$install_dir" ]]; then
        print_info "Директория $install_dir уже существует, обновляем..."
        cd "$install_dir"
        git pull
    else
        sudo git clone "$repo_url" "$install_dir"
        sudo chown -R $USER:$USER "$install_dir"
    fi
    
    cd "$install_dir"
    print_success "Репозиторий склонирован в $install_dir"
}

compile_project() {
    print_step "Компиляция проекта..."
    
    local build_dir="build"
    
    rm -rf "$build_dir"
    mkdir -p "$build_dir"
    cd "$build_dir"
    
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make -j$(nproc)
    
    cd ..
    print_success "Компиляция завершена"
}

init_databases() {
    print_step "Инициализация баз данных..."
    
    mkdir -p data
    for i in {1..3}; do
        sqlite3 "data/node${i}.db" < src/storage/schema.sql 2>/dev/null
    done
    
    print_success "Базы данных созданы"
}

ask_monitoring() {
    echo ""
    echo -e "${YELLOW}📊 Запустить контейнеры Prometheus и Grafana для мониторинга?${NC}"
    echo "   (На сервере обычно не нужно, для демонстрации на компьютере – полезно)"
    read -p "   Запустить? [y/N]: " -n 1 -r
    echo ""
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        setup_monitoring
    else
        print_info "Мониторинг не установлен (можно запустить позже: cd /opt/nexus-ledger && docker compose up -d)"
    fi
}

setup_monitoring() {
    print_step "Запуск Prometheus и Grafana через Docker Compose..."
    cd /opt/nexus-ledger
    if [[ -f "docker-compose.yml" ]]; then
        sudo docker compose up -d
        print_success "Prometheus и Grafana запущены"
        print_info "Grafana: http://localhost:3009 (admin/admin), Prometheus: http://localhost:9090"
    else
        print_error "Файл docker-compose.yml не найден, пропускаем"
    fi
}

setup_systemd() {
    print_step "Настройка systemd сервиса для нод..."
    
    local service_file="/etc/systemd/system/nexus-node@.service"
    sudo tee $service_file > /dev/null <<EOF
[Unit]
Description=Nexus Ledger Node %i
After=network.target

[Service]
Type=simple
User=$USER
WorkingDirectory=/opt/nexus-ledger/build
ExecStart=/opt/nexus-ledger/build/nexus-ledger node %i /opt/nexus-ledger/data/node%i.db 91%i
Restart=always
RestartSec=10
LimitNOFILE=65536

[Install]
WantedBy=multi-user.target
EOF

    sudo systemctl daemon-reload
    print_success "Сервис создан: nexus-node@.service"
}

print_usage() {
    echo ""
    echo -e "${CYAN}═══════════════════════════════════════════════════════════════════${NC}"
    echo -e "${GREEN}                    УСТАНОВКА ЗАВЕРШЕНА!                         ${NC}"
    echo -e "${CYAN}═══════════════════════════════════════════════════════════════════${NC}"
    echo ""
    echo -e "${YELLOW}📁  Директория установки:${NC} /opt/nexus-ledger"
    echo ""
    echo -e "${YELLOW}🚀  Управление нодами (systemd):${NC}"
    echo "    sudo systemctl start nexus-node@8000      # запуск ноды на порту 8000"
    echo "    sudo systemctl enable nexus-node@8000     # автозапуск"
    echo "    sudo systemctl status nexus-node@8000     # статус"
    echo "    journalctl -u nexus-node@8000 -f          # просмотр логов"
    echo ""
    echo -e "${YELLOW}📊  Мониторинг:${NC}"
    echo "    (если вы разрешили запуск контейнеров)"
    echo "    Prometheus: http://localhost:9090"
    echo "    Grafana: http://localhost:3009 (admin/admin)"
    echo ""
    echo -e "${YELLOW}🏭  Генератор нагрузки:${NC}"
    echo "    python3 /opt/nexus-ledger/scripts/load_generator.py"
    echo ""
    echo -e "${YELLOW}🔧  Ручной запуск (без systemd):${NC}"
    echo "    cd /opt/nexus-ledger/build"
    echo "    ./nexus-ledger node 8000 ../data/node1.db 9100"
    echo ""
    echo -e "${CYAN}═══════════════════════════════════════════════════════════════════${NC}"
}

# Главная функция
main() {
    print_banner
    
    echo -e "${YELLOW}Добро пожаловать в установщик Nexus Ledger!${NC}"
    echo ""
    
    read -p "Нажмите Enter для продолжения или Ctrl+C для отмены..."
    
    check_os
    update_system
    install_dependencies
    clone_repo
    compile_project
    init_databases
    ask_monitoring          # ← опрос про мониторинг
    setup_systemd
    
    print_usage
    
    echo -e "${YELLOW}⚠️  Чтобы изменения группы docker вступили в силу, перезагрузитесь или выполните:${NC}"
    echo "    newgrp docker"
    echo ""
}

# Запуск
main "$@"